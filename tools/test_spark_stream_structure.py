"""Host seam compiling the production SparkStreamReader structure and drain methods."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/SparkStreamReader.cpp").read_text()
consumer = (root / "src/SparkDataControl.cpp").read_text()


def method(signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for pos in range(opening, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[start:pos + 1]
    raise AssertionError(signature)


dispatch = consumer[consumer.index("void SparkDataControl::processSparkData("):
                    consumer.index("bool SparkDataControl::processAction(")]
assert "if (retCode == MSG_PROCESS_RES_COMPLETE)" in dispatch
assert "if (retCode == MSG_PROCESS_RES_REQUEST" in dispatch
assert "retCode = sparkSsr.nextMessage();" in dispatch
assert "handleIncomingAck();" in dispatch

host = r'''
#include "SparkReceiveAssembly.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <vector>
using byte = uint8_t;
using ByteVector = std::vector<byte>;
using std::vector;
using std::min;
using boolean = bool;
#define DEBUG_PRINTF(...) ((void)0)
enum MessageProcessStatus { MSG_PROCESS_RES_COMPLETE, MSG_PROCESS_RES_INCOMPLETE, MSG_PROCESS_RES_REQUEST, MSG_PROCESS_RES_REJECT };
struct CmdData { byte cmd = 0, subcmd = 0; ByteVector data; };
class SparkStreamReader {
public:
    struct Status { byte number = 0; byte &lastMessageNum() { return number; } } statusObject;
    vector<ByteVector> unstructuredData, response;
    vector<CmdData> message;
    std::deque<vector<ByteVector>> pendingMessages_;
    int interpretations = 0;
    void setMessage(const vector<ByteVector> &frames);
    boolean structureData(bool processHeader);
    ByteVector convertDataTo8bit(ByteVector input);
    vector<CmdData> readMessage(bool processHeader);
    MessageProcessStatus nextMessage();
    void interpretData() { ++interpretations; }
};
''' + '\n'.join(method(signature) for signature in (
    "void SparkStreamReader::setMessage(",
    "boolean SparkStreamReader::structureData(",
    "ByteVector SparkStreamReader::convertDataTo8bit(",
    "vector<CmdData> SparkStreamReader::readMessage(",
    "MessageProcessStatus SparkStreamReader::nextMessage(",
)) + r'''
static ByteVector wire(byte seq, byte cmd, byte sub, ByteVector data) {
    ByteVector frame = {0xF0, 0x01, seq, 0, cmd, sub};
    for (size_t pos = 0; pos < data.size(); pos += 7) {
        byte mask = 0;
        for (size_t i = pos; i < data.size() && i < pos + 7; ++i)
            if (data[i] & 0x80) mask |= byte(1 << (i - pos));
        frame.push_back(mask);
        for (size_t i = pos; i < data.size() && i < pos + 7; ++i)
            frame.push_back(data[i] & 0x7F);
    }
    for (size_t i = 6; i < frame.size(); ++i) frame[3] ^= frame[i];
    frame.push_back(0xF7);
    return frame;
}
static void single(byte seq) {
    SparkStreamReader reader;
    ByteVector payload = {0x81, 0x7F, 0xF7, 0x00, 0xFF, 0x80, 0x42, 0xCE};
    reader.setMessage({wire(seq, 3, 0x38, payload)});
    assert(reader.structureData(false));
    assert(reader.message.size() == 1 && reader.message[0].data == payload);
    assert(reader.message[0].cmd == 3 && reader.message[0].subcmd == 0x38);
    assert(reader.statusObject.lastMessageNum() == seq);
}
static void multipart() {
    SparkStreamReader reader;
    vector<ByteVector> frames;
    ByteVector expected;
    for (byte i = 0; i < 17; ++i) {
        const byte length = i == 16 ? 5 : 25;
        ByteVector chunk = {17, i, length};
        for (byte j = 0; j < length; ++j) chunk.push_back(byte(0x80 | ((i + j) & 0x7F)));
        expected.insert(expected.end(), chunk.begin() + 3, chunk.end());
        frames.push_back(wire(0xF7, 3, 1, chunk));
    }
    reader.setMessage(frames);
    assert(reader.structureData(false));
    assert(reader.message.size() == 1 && reader.message[0].data == expected);
    assert(reader.statusObject.lastMessageNum() == 0xF7);
}
static void multiple() {
    SparkStreamReader reader;
    auto a = wire(0xFF, 3, 0x38, {0x82, 0xF7});
    auto b = wire(0x01, 3, 0x63, {0x80, 0xAA});
    reader.setMessage({a, b});
    assert(reader.structureData(false));
    assert(reader.message.size() == 2);
    assert(reader.message[0].data == ByteVector({0x82, 0xF7}));
    assert(reader.message[1].data == ByteVector({0x80, 0xAA}));
    assert(reader.statusObject.lastMessageNum() == 1);
    // Optional 16-byte transport header around each complete frame.
    vector<ByteVector> blocks;
    for (const auto &frame : {a, b}) {
        ByteVector block(16, 0);
        block.insert(block.end(), frame.begin(), frame.end());
        block[6] = byte(block.size());
        blocks.push_back(block);
    }
    reader.setMessage(blocks);
    assert(reader.structureData(true) && reader.message.size() == 2);
}
static void rejected() {
    SparkStreamReader reader;
    auto good = wire(0xF6, 3, 0x38, {1, 2});
    reader.pendingMessages_.push_back({good});
    assert(reader.nextMessage() == MSG_PROCESS_RES_COMPLETE);
    assert(reader.statusObject.lastMessageNum() == 0xF6);
    const int interpreted = reader.interpretations;
    // A malformed frame following a valid one must not leave partial parsed data.
    auto bad = wire(0xF7, 3, 1, {1});
    bad.pop_back();
    reader.setMessage({good, bad});
    assert(!reader.structureData(false) && reader.message.empty());
    reader.pendingMessages_.push_back({bad});
    reader.pendingMessages_.push_back({});
    // Valid frame but only the first part: structure succeeds without a message.
    reader.pendingMessages_.push_back({wire(0xF7, 3, 1, {2, 0, 25,
        0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81,
        0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81,
        0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81})});
    reader.pendingMessages_.push_back({wire(0xF7, 3, 0x38, {3, 4})});
    assert(reader.nextMessage() == MSG_PROCESS_RES_REJECT);
    assert(reader.message.empty() && reader.statusObject.lastMessageNum() == 0);
    assert(reader.interpretations == interpreted);
    assert(reader.nextMessage() == MSG_PROCESS_RES_REJECT);
    assert(reader.nextMessage() == MSG_PROCESS_RES_REJECT);
    assert(reader.nextMessage() == MSG_PROCESS_RES_COMPLETE);
    assert(reader.statusObject.lastMessageNum() == 0xF7);
    assert(reader.interpretations == interpreted + 1);
    reader.setMessage({{0xF0, 0x01, 0xF7}});
    assert(!reader.structureData(false));
    reader.setMessage({{0, 1, 2, 3, 4, 5, 0xF7}});
    assert(!reader.structureData(false));
    reader.setMessage({wire(1, 3, 0x38, {1}), {}});
    assert(!reader.structureData(false));
}
int main() {
    single(42); single(0xF7); single(0xF0); single(0xFF); single(1);
    multipart(); multiple(); rejected();
}
'''

with tempfile.TemporaryDirectory(dir="/tmp/opencode") as temporary:
    path = Path(temporary) / "stream_structure.cpp"
    executable = Path(temporary) / "stream_structure"
    path.write_text(host)
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-Isrc", str(path), "-o", str(executable)], cwd=root, check=True)
    subprocess.run([str(executable)], check=True)
print("Spark stream structure/drain: PASS")
