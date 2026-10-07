"""Compile the production receive/drain methods with a host-only parser seam.

The production reader pulls in Arduino state/preset decoding; this harness
replaces only readMessage() with an observation hook. Framing, queueing and
draining are the actual SparkStreamReader method bodies.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
reader = (root / "src/SparkStreamReader.cpp").read_text()
consumer = (root / "src/SparkDataControl.cpp").read_text()


def method(source, signature):
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


receive = method(reader, "MessageProcessStatus SparkStreamReader::processBlock(")
drain = method(reader, "MessageProcessStatus SparkStreamReader::nextMessage(")
dispatch = method(consumer, "void SparkDataControl::processSparkData(")
assert "while (retCode != MSG_PROCESS_RES_INCOMPLETE)" in dispatch
assert "retCode = sparkSsr.nextMessage();" in dispatch
assert dispatch.index("handleAppModeResponse();") < dispatch.index("retCode = sparkSsr.nextMessage();")
assert dispatch.index("responseLane_.complete(") < dispatch.index("retCode = sparkSsr.nextMessage();")
assert dispatch.index("retCode = sparkSsr.nextMessage();") < dispatch.index("serviceRetainedIntents();")

host = r'''
#include "SparkReceiveAssembly.h"
#include <cassert>
#include <cstdint>
#include <deque>
#include <vector>
using byte = uint8_t;
using ByteVector = std::vector<byte>;
using std::vector;
enum MessageProcessStatus { MSG_PROCESS_RES_COMPLETE, MSG_PROCESS_RES_INCOMPLETE, MSG_PROCESS_RES_REQUEST, MSG_PROCESS_RES_REJECT };
class SparkStreamReader {
public:
    SparkReceiveAssembly assembly_;
    SparkReceiveFrames frameReader_;
    vector<ByteVector> response, unstructuredData;
    std::deque<vector<ByteVector>> pendingMessages_;
    vector<byte> lastData;
    vector<byte> message;
    byte lastNumber = 0, lastCmd = 0, lastSub = 0;
    void setMessage(const vector<ByteVector> &data) { unstructuredData = data; message.clear(); }
    vector<byte> readMessage(bool) {
        assert(!unstructuredData.empty());
        lastData.clear();
        lastNumber = unstructuredData.back()[2];
        lastCmd = unstructuredData.back()[4];
        lastSub = unstructuredData.back()[5];
        for (const auto &frame : unstructuredData) {
            auto data = SparkReceiveAssembly::decoded(frame);
            if (lastSub == 1 && SparkReceiveAssembly::presetShape(frame, data) == 2)
                data.erase(data.begin(), data.begin() + 3);
            lastData.insert(lastData.end(), data.begin(), data.end());
        }
        message = lastData;
        return message;
    }
    struct Status { byte number = 0; byte &lastMessageNum() { return number; } } statusObject;
    MessageProcessStatus processBlock(ByteVector &blk);
    MessageProcessStatus nextMessage();
};
''' + receive + '\n' + drain + r'''
static ByteVector wire(byte seq, byte sub, ByteVector data) {
    ByteVector frame = {0xF0, 0x01, seq, 0, 3, sub};
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
static ByteVector part(byte seq, byte idx, byte length) {
    ByteVector data = {2, idx, length};
    data.insert(data.end(), length, byte(0x81));
    return wire(seq, 1, data);
}
static void verify(bool presetFirst) {
    SparkStreamReader reader;
    const auto a = part(16, 0, 25);
    const auto b = part(16, 1, 5);
    const auto single = wire(41, 0x38, {0, 3});
    ByteVector input = a;
    assert(reader.processBlock(input) == MSG_PROCESS_RES_INCOMPLETE);
    // Both completed messages arrive in one BLE block, in either order.
    input = presetFirst ? b : single;
    input.insert(input.end(), presetFirst ? single.begin() : b.begin(),
                 presetFirst ? single.end() : b.end());
    auto status = reader.processBlock(input);
    int callbacks = 0, correlations = 0, stateUpdates = 0;
    while (status != MSG_PROCESS_RES_INCOMPLETE) {
        assert(status == MSG_PROCESS_RES_COMPLETE);
        if (callbacks == 0) assert((reader.lastSub == 1) == presetFirst);
        if (reader.lastSub == 1) {
            assert(reader.lastNumber == 16 && reader.lastData.size() == 30);
            assert(reader.lastData.front() == 0x81);
            ++correlations; // matching response lane owner
        } else {
            assert(reader.lastNumber == 41 && reader.lastSub == 0x38);
            assert(reader.lastData == ByteVector({0, 3}));
            ++stateUpdates; // unsolicited hardware-number observation
        }
        ++callbacks;
        status = reader.nextMessage();
    }
    assert(callbacks == 2 && correlations == 1 && stateUpdates == 1);
}
int main() { verify(true); verify(false); }
'''

with tempfile.TemporaryDirectory(dir="/tmp/opencode") as temporary:
    source = Path(temporary) / "receive_batch.cpp"
    executable = Path(temporary) / "receive_batch"
    source.write_text(host)
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-Isrc", str(source), "-o", str(executable)], cwd=root, check=True)
    subprocess.run([str(executable)], check=True)
print("Spark receive batch/drain: PASS")
