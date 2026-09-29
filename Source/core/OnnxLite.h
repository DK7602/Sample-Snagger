#pragma once

#include <juce_core/juce_core.h>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace snag::onnx
{

/** A tensor: float data, or integer data (shapes, indices, booleans). */
struct Tensor
{
    std::vector<int64_t> shape;
    std::vector<float> f;
    std::vector<int64_t> i;
    bool isInt = false;

    size_t size() const noexcept
    {
        size_t n = 1;
        for (auto d : shape) n *= (size_t) juce::jmax<int64_t> (0, d);
        return n;
    }
    static Tensor floats (std::vector<int64_t> s, std::vector<float> d = {});
    static Tensor ints (std::vector<int64_t> s, std::vector<int64_t> d = {});
};

/** Just enough ONNX to run small convolutional models (such as Spotify's Basic Pitch) with no
    external runtime: a hand-written protobuf reader and the ~25 operators those models use. */
class Model
{
public:
    /** Parses the model; check isValid() / getError(). */
    explicit Model (const void* data, size_t size);

    bool isValid() const noexcept                   { return error.isEmpty() && ! nodes.empty(); }
    const juce::String& getError() const noexcept   { return error; }
    const std::vector<std::string>& inputNames() const noexcept  { return inputs; }
    const std::vector<std::string>& outputNames() const noexcept { return outputs; }

    /** Runs the graph (thread-safe: each call keeps its own intermediate tensors). */
    bool run (const std::map<std::string, Tensor>& feeds, std::map<std::string, Tensor>& results, juce::String& err) const;

private:
    struct Attr
    {
        int64_t i = 0;
        float f = 0;
        std::string s;
        std::vector<int64_t> ints;
        std::vector<float> floats;
        bool has = false;
    };
    struct Node
    {
        std::string op;
        std::vector<std::string> in, out;
        std::map<std::string, Attr> attrs;
    };

    std::vector<Node> nodes;
    std::map<std::string, Tensor> initializers;
    std::vector<std::string> inputs, outputs;
    juce::String error;

    static bool exec (const Node&, const std::vector<const Tensor*>& in, Tensor& out, juce::String& err);
};

} // namespace snag::onnx
