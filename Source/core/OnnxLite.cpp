#include "OnnxLite.h"
#include <cmath>
#include <cstring>
#include <functional>
#include <numeric>
#include <set>

namespace snag::onnx
{

Tensor Tensor::floats (std::vector<int64_t> s, std::vector<float> d)
{
    Tensor t; t.shape = std::move (s); t.isInt = false;
    t.f = d.empty() ? std::vector<float> (t.size(), 0.0f) : std::move (d);
    return t;
}

Tensor Tensor::ints (std::vector<int64_t> s, std::vector<int64_t> d)
{
    Tensor t; t.shape = std::move (s); t.isInt = true;
    t.i = d.empty() ? std::vector<int64_t> (t.size(), 0) : std::move (d);
    return t;
}

//==============================================================================
namespace
{
    // ---- protobuf wire format -------------------------------------------------------------
    struct Reader
    {
        const uint8_t* p;
        const uint8_t* end;
        bool ok = true;

        Reader (const void* d, size_t n) : p ((const uint8_t*) d), end ((const uint8_t*) d + n) {}
        bool more() const noexcept { return ok && p < end; }

        uint64_t varint()
        {
            uint64_t r = 0;
            for (int shift = 0; shift < 64; shift += 7)
            {
                if (p >= end) { ok = false; return 0; }
                const uint8_t c = *p++;
                r |= (uint64_t) (c & 0x7f) << shift;
                if (c < 0x80) return r;
            }
            ok = false;
            return 0;
        }
        Reader bytes()
        {
            const auto n = (size_t) varint();
            if (! ok || (size_t) (end - p) < n) { ok = false; return Reader (p, 0); }
            Reader r (p, n);
            p += n;
            return r;
        }
        uint32_t fixed32()
        {
            if (end - p < 4) { ok = false; return 0; }
            uint32_t v; std::memcpy (&v, p, 4); p += 4; return v;
        }
        void skip (int wire)
        {
            if (wire == 0) varint();
            else if (wire == 1) { if (end - p < 8) ok = false; else p += 8; }
            else if (wire == 2) bytes();
            else if (wire == 5) fixed32();
            else ok = false;
        }
        std::string str() { auto b = bytes(); return std::string ((const char*) b.p, (size_t) (b.end - b.p)); }
    };

    int64_t asSigned (uint64_t v) noexcept { return (int64_t) v; }

    void readInts (Reader& r, int wire, std::vector<int64_t>& out)
    {
        if (wire == 2) { auto b = r.bytes(); while (b.more()) out.push_back (asSigned (b.varint())); }
        else out.push_back (asSigned (r.varint()));
    }
    void readFloats (Reader& r, int wire, std::vector<float>& out)
    {
        auto one = [&] (Reader& rr) { const uint32_t u = rr.fixed32(); float f; std::memcpy (&f, &u, 4); out.push_back (f); };
        if (wire == 2) { auto b = r.bytes(); while (b.more()) one (b); }
        else one (r);
    }

    bool parseTensor (Reader r, std::string& name, Tensor& t)
    {
        int dataType = 1;
        std::vector<int64_t> dims, i64;
        std::vector<float> fl;
        const uint8_t* raw = nullptr; size_t rawSize = 0;
        while (r.more())
        {
            const auto key = r.varint();
            const int field = (int) (key >> 3), wire = (int) (key & 7);
            if (field == 1) readInts (r, wire, dims);
            else if (field == 2) dataType = (int) r.varint();
            else if (field == 8) name = r.str();
            else if (field == 9) { auto b = r.bytes(); raw = b.p; rawSize = (size_t) (b.end - b.p); }
            else if (field == 4) readFloats (r, wire, fl);
            else if (field == 7 || field == 5) readInts (r, wire, i64);
            else r.skip (wire);
        }
        if (! r.ok) return false;
        t.shape = dims;
        const size_t n = t.size();
        if (dataType == 1)
        {
            t.isInt = false;
            if (raw != nullptr) { t.f.resize (rawSize / 4); std::memcpy (t.f.data(), raw, t.f.size() * 4); }
            else t.f = fl;
            return t.f.size() == n;
        }
        t.isInt = true;
        if (raw != nullptr)
        {
            const size_t w = dataType == 7 ? 8 : (dataType == 6 ? 4 : 1);
            t.i.resize (rawSize / w);
            for (size_t k = 0; k < t.i.size(); ++k)
            {
                if (w == 8) { int64_t v; std::memcpy (&v, raw + k * 8, 8); t.i[k] = v; }
                else if (w == 4) { int32_t v; std::memcpy (&v, raw + k * 4, 4); t.i[k] = v; }
                else t.i[k] = raw[k] != 0 ? 1 : 0;
            }
        }
        else t.i = i64;
        return t.i.size() == n;
    }

    // ---- tensor helpers -----------------------------------------------------------------
    std::vector<int64_t> stridesOf (const std::vector<int64_t>& shape)
    {
        std::vector<int64_t> s (shape.size(), 1);
        for (int k = (int) shape.size() - 2; k >= 0; --k)
            s[(size_t) k] = s[(size_t) k + 1] * shape[(size_t) k + 1];
        return s;
    }

    double getD (const Tensor& t, size_t k) noexcept { return t.isInt ? (double) t.i[k] : (double) t.f[k]; }

    std::vector<int64_t> asInts (const Tensor* t)
    {
        std::vector<int64_t> r;
        if (t == nullptr) return r;
        for (size_t k = 0; k < t->size(); ++k) r.push_back (t->isInt ? t->i[k] : (int64_t) t->f[k]);
        return r;
    }

    bool broadcastShape (const std::vector<const Tensor*>& ts, std::vector<int64_t>& out)
    {
        size_t rank = 0;
        for (auto* t : ts) rank = juce::jmax (rank, t->shape.size());
        out.assign (rank, 1);
        for (auto* t : ts)
            for (size_t k = 0; k < t->shape.size(); ++k)
            {
                const size_t o = rank - t->shape.size() + k;
                const auto d = t->shape[k];
                if (d == out[o] || d == 1) continue;
                if (out[o] == 1) out[o] = d; else return false;
            }
        return true;
    }

    /** Calls fn(outIndex, offsets...) for every element of the broadcast result. */
    void forEachBroadcast (const std::vector<int64_t>& outShape, const std::vector<const Tensor*>& ts,
                           const std::function<void (size_t, const size_t*)>& fn)
    {
        const size_t rank = outShape.size(), n = ts.size();
        std::vector<std::vector<int64_t>> st (n, std::vector<int64_t> (rank, 0));
        for (size_t a = 0; a < n; ++a)
        {
            auto s = stridesOf (ts[a]->shape);
            for (size_t k = 0; k < ts[a]->shape.size(); ++k)
            {
                const size_t o = rank - ts[a]->shape.size() + k;
                st[a][o] = ts[a]->shape[k] == 1 ? 0 : s[k];
            }
        }
        size_t total = 1;
        for (auto d : outShape) total *= (size_t) d;
        std::vector<int64_t> idx (rank, 0);
        std::vector<size_t> off (n, 0);
        for (size_t e = 0; e < total; ++e)
        {
            fn (e, off.data());
            for (int k = (int) rank - 1; k >= 0; --k)
            {
                ++idx[(size_t) k];
                for (size_t a = 0; a < n; ++a) off[a] += (size_t) st[a][(size_t) k];
                if (idx[(size_t) k] < outShape[(size_t) k]) break;
                for (size_t a = 0; a < n; ++a) off[a] -= (size_t) (st[a][(size_t) k] * outShape[(size_t) k]);
                idx[(size_t) k] = 0;
            }
        }
    }

    /** Copies a strided view: for each output index, the input offset is base + sum(idx_k * step_k). */
    template <typename T>
    void gather (const std::vector<T>& src, std::vector<T>& dst, const std::vector<int64_t>& outShape,
                 int64_t base, const std::vector<int64_t>& steps)
    {
        size_t total = 1;
        for (auto d : outShape) total *= (size_t) d;
        dst.resize (total);
        const size_t rank = outShape.size();
        std::vector<int64_t> idx (rank, 0);
        int64_t off = base;
        for (size_t e = 0; e < total; ++e)
        {
            dst[e] = src[(size_t) off];
            for (int k = (int) rank - 1; k >= 0; --k)
            {
                ++idx[(size_t) k]; off += steps[(size_t) k];
                if (idx[(size_t) k] < outShape[(size_t) k]) break;
                off -= steps[(size_t) k] * outShape[(size_t) k];
                idx[(size_t) k] = 0;
            }
        }
    }

    int64_t normAxis (int64_t a, size_t rank) noexcept { return a < 0 ? a + (int64_t) rank : a; }
}

//==============================================================================
Model::Model (const void* data, size_t size)
{
    Reader model (data, size);
    Reader graph (nullptr, 0);
    bool haveGraph = false;
    while (model.more())
    {
        const auto key = model.varint();
        const int field = (int) (key >> 3), wire = (int) (key & 7);
        if (field == 7 && wire == 2) { graph = model.bytes(); haveGraph = true; }
        else model.skip (wire);
    }
    if (! model.ok || ! haveGraph) { error = "Not an ONNX model"; return; }

    std::vector<std::string> graphInputs;
    while (graph.more())
    {
        const auto key = graph.varint();
        const int field = (int) (key >> 3), wire = (int) (key & 7);
        if (field == 1)
        {
            auto r = graph.bytes();
            Node n;
            while (r.more())
            {
                const auto k2 = r.varint();
                const int f2 = (int) (k2 >> 3), w2 = (int) (k2 & 7);
                if (f2 == 1) n.in.push_back (r.str());
                else if (f2 == 2) n.out.push_back (r.str());
                else if (f2 == 4) n.op = r.str();
                else if (f2 == 5)
                {
                    auto ar = r.bytes();
                    std::string name; Attr a; a.has = true;
                    while (ar.more())
                    {
                        const auto k3 = ar.varint();
                        const int f3 = (int) (k3 >> 3), w3 = (int) (k3 & 7);
                        if (f3 == 1) name = ar.str();
                        else if (f3 == 2) { const uint32_t u = ar.fixed32(); std::memcpy (&a.f, &u, 4); }
                        else if (f3 == 3) a.i = asSigned (ar.varint());
                        else if (f3 == 4) a.s = ar.str();
                        else if (f3 == 7) readFloats (ar, w3, a.floats);
                        else if (f3 == 8) readInts (ar, w3, a.ints);
                        else ar.skip (w3);
                    }
                    n.attrs[name] = a;
                }
                else r.skip (w2);
            }
            nodes.push_back (std::move (n));
        }
        else if (field == 5)
        {
            std::string name; Tensor t;
            if (! parseTensor (graph.bytes(), name, t)) { error = "Bad weights in the model"; return; }
            initializers[name] = std::move (t);
        }
        else if (field == 11 || field == 12)
        {
            auto r = graph.bytes();
            std::string name;
            while (r.more())
            {
                const auto k2 = r.varint();
                if ((k2 >> 3) == 1) name = r.str(); else r.skip ((int) (k2 & 7));
            }
            (field == 11 ? graphInputs : outputs).push_back (name);
        }
        else graph.skip (wire);
    }
    if (! graph.ok) { error = "Damaged ONNX model"; return; }
    for (auto& n : graphInputs)
        if (initializers.find (n) == initializers.end())
            inputs.push_back (n);
}

bool Model::run (const std::map<std::string, Tensor>& feeds, std::map<std::string, Tensor>& results, juce::String& err) const
{
    // free intermediate tensors after their last use
    std::map<std::string, size_t> lastUse;
    for (size_t k = 0; k < nodes.size(); ++k)
        for (auto& in : nodes[k].in) lastUse[in] = k;
    const std::set<std::string> keep (outputs.begin(), outputs.end());

    std::map<std::string, Tensor> env;
    auto lookup = [&] (const std::string& name) -> const Tensor*
    {
        if (name.empty()) return nullptr;
        if (auto it = env.find (name); it != env.end()) return &it->second;
        if (auto it = feeds.find (name); it != feeds.end()) return &it->second;
        if (auto it = initializers.find (name); it != initializers.end()) return &it->second;
        return nullptr;
    };

    for (size_t k = 0; k < nodes.size(); ++k)
    {
        const auto& n = nodes[k];
        std::vector<const Tensor*> in;
        for (auto& name : n.in)
        {
            in.push_back (lookup (name));
            if (! name.empty() && in.back() == nullptr) { err = "Missing tensor " + juce::String (name); return false; }
        }
        Tensor out;
        if (! exec (n, in, out, err))
        {
            err = juce::String (n.op) + ": " + err;
            return false;
        }
        env[n.out.empty() ? std::string() : n.out[0]] = std::move (out);
        for (auto& name : n.in)
            if (auto it = lastUse.find (name); it != lastUse.end() && it->second == k && keep.count (name) == 0)
                env.erase (name);
    }
    for (auto& o : outputs)
    {
        auto* t = lookup (o);
        if (t == nullptr) { err = "No output " + juce::String (o); return false; }
        results[o] = *t;
    }
    return true;
}

//==============================================================================
bool Model::exec (const Node& n, const std::vector<const Tensor*>& in, Tensor& out, juce::String& err)
{
    auto attrI = [&] (const char* name, int64_t def) { auto it = n.attrs.find (name); return it != n.attrs.end() ? it->second.i : def; };
    auto attrInts = [&] (const char* name) { auto it = n.attrs.find (name); return it != n.attrs.end() ? it->second.ints : std::vector<int64_t>(); };
    auto attrS = [&] (const char* name) { auto it = n.attrs.find (name); return it != n.attrs.end() ? it->second.s : std::string(); };
    const auto& op = n.op;
    if (in.empty() || in[0] == nullptr) { err = "no input"; return false; }
    const Tensor& x = *in[0];

    if (op == "Reshape" || op == "Unsqueeze" || op == "Squeeze")
    {
        std::vector<int64_t> shape;
        if (op == "Reshape")
        {
            shape = asInts (in.size() > 1 ? in[1] : nullptr);
            int64_t known = 1; int minus = -1;
            for (size_t k = 0; k < shape.size(); ++k)
            {
                if (shape[k] == 0 && k < x.shape.size()) shape[k] = x.shape[k];
                if (shape[k] == -1) minus = (int) k; else known *= shape[k];
            }
            if (minus >= 0) shape[(size_t) minus] = known > 0 ? (int64_t) x.size() / known : 0;
        }
        else if (op == "Unsqueeze")
        {
            auto axes = in.size() > 1 && in[1] != nullptr ? asInts (in[1]) : attrInts ("axes");
            const size_t rank = x.shape.size() + axes.size();
            for (auto& a : axes) a = normAxis (a, rank);
            std::sort (axes.begin(), axes.end());
            shape = x.shape;
            for (auto a : axes) shape.insert (shape.begin() + a, 1);
        }
        else
        {
            auto axes = in.size() > 1 && in[1] != nullptr ? asInts (in[1]) : attrInts ("axes");
            for (auto& a : axes) a = normAxis (a, x.shape.size());
            for (size_t k = 0; k < x.shape.size(); ++k)
                if (axes.empty() ? x.shape[k] != 1 : std::find (axes.begin(), axes.end(), (int64_t) k) == axes.end())
                    shape.push_back (x.shape[k]);
        }
        out = x;
        out.shape = shape;
        if (out.size() != x.size()) { err = "bad shape"; return false; }
        return true;
    }

    if (op == "Shape")
    {
        out = Tensor::ints ({ (int64_t) x.shape.size() }, x.shape);
        if (x.shape.empty()) out = Tensor::ints ({ 0 });
        return true;
    }

    if (op == "Cast")
    {
        const auto to = attrI ("to", 1);
        out.shape = x.shape;
        if (to == 1 || to == 11) { out.isInt = false; out.f.resize (x.size()); for (size_t k = 0; k < x.size(); ++k) out.f[k] = (float) getD (x, k); }
        else
        {
            out.isInt = true; out.i.resize (x.size());
            for (size_t k = 0; k < x.size(); ++k)
                out.i[k] = to == 9 ? (getD (x, k) != 0.0 ? 1 : 0) : (int64_t) getD (x, k);
        }
        return true;
    }

    if (op == "Neg" || op == "Sqrt" || op == "Log" || op == "Relu" || op == "Sigmoid")
    {
        out.shape = x.shape;
        if (x.isInt && op == "Neg") { out.isInt = true; out.i.resize (x.size()); for (size_t k = 0; k < x.size(); ++k) out.i[k] = -x.i[k]; return true; }
        out.isInt = false;
        out.f.resize (x.size());
        for (size_t k = 0; k < x.size(); ++k)
        {
            const float v = (float) getD (x, k);
            float r = v;
            if (op == "Neg") r = -v;
            else if (op == "Sqrt") r = std::sqrt (v);
            else if (op == "Log") r = std::log (v);
            else if (op == "Relu") r = v > 0 ? v : 0.0f;
            else r = 1.0f / (1.0f + std::exp (-v));
            out.f[k] = r;
        }
        return true;
    }

    if (op == "Add" || op == "Sub" || op == "Mul" || op == "Div" || op == "Equal")
    {
        if (in.size() < 2 || in[1] == nullptr) { err = "needs two inputs"; return false; }
        const Tensor& y = *in[1];
        std::vector<const Tensor*> ts { &x, &y };
        if (! broadcastShape (ts, out.shape)) { err = "shapes don't broadcast"; return false; }
        const int kind = op == "Add" ? 0 : op == "Sub" ? 1 : op == "Mul" ? 2 : op == "Div" ? 3 : 4;
        size_t total = 1; for (auto d : out.shape) total *= (size_t) d;
        if (kind == 4 || (x.isInt && y.isInt))
        {
            out.isInt = true; out.i.resize (total);
            forEachBroadcast (out.shape, ts, [&] (size_t e, const size_t* o)
            {
                const int64_t a = x.isInt ? x.i[o[0]] : (int64_t) x.f[o[0]], b = y.isInt ? y.i[o[1]] : (int64_t) y.f[o[1]];
                if (kind == 4) out.i[e] = getD (x, o[0]) == getD (y, o[1]) ? 1 : 0;
                else out.i[e] = kind == 0 ? a + b : kind == 1 ? a - b : kind == 2 ? a * b : (b != 0 ? a / b : 0);
            });
        }
        else
        {
            out.isInt = false; out.f.resize (total);
            forEachBroadcast (out.shape, ts, [&] (size_t e, const size_t* o)
            {
                const float a = (float) getD (x, o[0]), b = (float) getD (y, o[1]);
                out.f[e] = kind == 0 ? a + b : kind == 1 ? a - b : kind == 2 ? a * b : a / b;
            });
        }
        return true;
    }

    if (op == "Where")
    {
        if (in.size() < 3 || in[1] == nullptr || in[2] == nullptr) { err = "needs three inputs"; return false; }
        const Tensor &a = *in[1], &b = *in[2];
        std::vector<const Tensor*> ts { &x, &a, &b };
        if (! broadcastShape (ts, out.shape)) { err = "shapes don't broadcast"; return false; }
        size_t total = 1; for (auto d : out.shape) total *= (size_t) d;
        out.isInt = a.isInt && b.isInt;
        if (out.isInt) out.i.resize (total); else out.f.resize (total);
        forEachBroadcast (out.shape, ts, [&] (size_t e, const size_t* o)
        {
            const bool c = getD (x, o[0]) != 0.0;
            if (out.isInt) out.i[e] = c ? a.i[o[1]] : b.i[o[2]];
            else out.f[e] = (float) (c ? getD (a, o[1]) : getD (b, o[2]));
        });
        return true;
    }

    if (op == "ReduceSum" || op == "ReduceMin" || op == "ReduceMax")
    {
        auto axes = in.size() > 1 && in[1] != nullptr ? asInts (in[1]) : attrInts ("axes");
        const bool keepDims = attrI ("keepdims", 1) != 0;
        if (axes.empty() && attrI ("noop_with_empty_axes", 0) != 0) { out = x; return true; }
        const size_t rank = x.shape.size();
        std::vector<bool> red (rank, axes.empty());
        for (auto a : axes) red[(size_t) normAxis (a, rank)] = true;
        std::vector<int64_t> kshape (rank);
        for (size_t k = 0; k < rank; ++k) kshape[k] = red[k] ? 1 : x.shape[k];
        const int kind = op == "ReduceSum" ? 0 : op == "ReduceMin" ? 1 : 2;
        Tensor acc = Tensor::floats (kshape);
        std::fill (acc.f.begin(), acc.f.end(), kind == 0 ? 0.0f : (kind == 1 ? std::numeric_limits<float>::max() : -std::numeric_limits<float>::max()));
        auto ks = stridesOf (kshape);
        std::vector<int64_t> idx (rank, 0);
        for (size_t e = 0; e < x.size(); ++e)
        {
            size_t o = 0;
            for (size_t k = 0; k < rank; ++k) if (! red[k]) o += (size_t) (idx[k] * ks[k]);
            const float v = (float) getD (x, e);
            float& a = acc.f[o];
            a = kind == 0 ? a + v : (kind == 1 ? juce::jmin (a, v) : juce::jmax (a, v));
            for (int k = (int) rank - 1; k >= 0; --k) { if (++idx[(size_t) k] < x.shape[(size_t) k]) break; idx[(size_t) k] = 0; }
        }
        out = std::move (acc);
        if (! keepDims)
        {
            std::vector<int64_t> s;
            for (size_t k = 0; k < rank; ++k) if (! red[k]) s.push_back (x.shape[k]);
            out.shape = s;
        }
        return true;
    }

    if (op == "Transpose")
    {
        auto perm = attrInts ("perm");
        const size_t rank = x.shape.size();
        if (perm.empty()) for (size_t k = 0; k < rank; ++k) perm.push_back ((int64_t) (rank - 1 - k));
        auto st = stridesOf (x.shape);
        std::vector<int64_t> shape (rank), steps (rank);
        for (size_t k = 0; k < rank; ++k) { shape[k] = x.shape[(size_t) perm[k]]; steps[k] = st[(size_t) perm[k]]; }
        out.shape = shape; out.isInt = x.isInt;
        if (x.isInt) gather (x.i, out.i, shape, 0, steps); else gather (x.f, out.f, shape, 0, steps);
        return true;
    }

    if (op == "Slice")
    {
        auto starts = asInts (in.size() > 1 ? in[1] : nullptr), ends = asInts (in.size() > 2 ? in[2] : nullptr);
        auto axes = in.size() > 3 && in[3] != nullptr ? asInts (in[3]) : std::vector<int64_t>();
        auto stepsIn = in.size() > 4 && in[4] != nullptr ? asInts (in[4]) : std::vector<int64_t>();
        const size_t rank = x.shape.size();
        if (axes.empty()) for (size_t k = 0; k < starts.size(); ++k) axes.push_back ((int64_t) k);
        std::vector<int64_t> begin (rank, 0), step (rank, 1), shape = x.shape;
        for (size_t k = 0; k < axes.size(); ++k)
        {
            const auto a = (size_t) normAxis (axes[k], rank);
            const int64_t dim = x.shape[a];
            const int64_t stp = stepsIn.size() > k ? stepsIn[k] : 1;
            int64_t s = starts[k], e = ends[k];
            if (s < 0) s += dim;
            if (e < 0 && e > std::numeric_limits<int64_t>::min() / 2) e += dim;
            if (stp > 0) { s = juce::jlimit<int64_t> (0, dim, s); e = juce::jlimit<int64_t> (0, dim, e); shape[a] = juce::jmax<int64_t> (0, (e - s + stp - 1) / stp); }
            else { s = juce::jlimit<int64_t> (0, dim - 1, s); e = juce::jlimit<int64_t> (-1, dim - 1, e); shape[a] = juce::jmax<int64_t> (0, (s - e - stp - 1) / -stp); }
            begin[a] = s; step[a] = stp;
        }
        auto st = stridesOf (x.shape);
        int64_t base = 0;
        std::vector<int64_t> steps (rank);
        for (size_t k = 0; k < rank; ++k) { base += begin[k] * st[k]; steps[k] = st[k] * step[k]; }
        out.shape = shape; out.isInt = x.isInt;
        size_t total = 1; for (auto d : shape) total *= (size_t) d;
        if (total == 0) { out.f.clear(); out.i.clear(); return true; }
        if (x.isInt) gather (x.i, out.i, shape, base, steps); else gather (x.f, out.f, shape, base, steps);
        return true;
    }

    if (op == "Concat")
    {
        const size_t rank = x.shape.size();
        const auto axis = (size_t) normAxis (attrI ("axis", 0), rank);
        out.shape = x.shape; out.shape[axis] = 0;
        bool anyFloat = false;
        for (auto* t : in) { out.shape[axis] += t->shape[axis]; anyFloat = anyFloat || ! t->isInt; }
        out.isInt = ! anyFloat;
        size_t outer = 1, inner = 1;
        for (size_t k = 0; k < axis; ++k) outer *= (size_t) x.shape[k];
        for (size_t k = axis + 1; k < rank; ++k) inner *= (size_t) x.shape[k];
        const size_t total = outer * (size_t) out.shape[axis] * inner;
        if (out.isInt) out.i.resize (total); else out.f.resize (total);
        size_t pos = 0;
        for (size_t o = 0; o < outer; ++o)
            for (auto* t : in)
            {
                const size_t chunk = (size_t) t->shape[axis] * inner;
                for (size_t q = 0; q < chunk; ++q, ++pos)
                {
                    if (out.isInt) out.i[pos] = t->i[o * chunk + q];
                    else out.f[pos] = (float) getD (*t, o * chunk + q);
                }
            }
        return true;
    }

    if (op == "Pad")
    {
        auto pads = asInts (in.size() > 1 ? in[1] : nullptr);
        const float cv = in.size() > 2 && in[2] != nullptr && in[2]->size() > 0 ? (float) getD (*in[2], 0) : 0.0f;
        const bool reflect = attrS ("mode") == "reflect";
        const size_t rank = x.shape.size();
        if (pads.size() != rank * 2) { err = "bad pads"; return false; }
        std::vector<int64_t> shape (rank);
        for (size_t k = 0; k < rank; ++k) shape[k] = x.shape[k] + pads[k] + pads[k + rank];
        out = Tensor::floats (shape);
        auto st = stridesOf (x.shape);
        std::vector<int64_t> idx (rank, 0);
        for (size_t e = 0; e < out.size(); ++e)
        {
            int64_t off = 0; bool inside = true;
            for (size_t k = 0; k < rank; ++k)
            {
                int64_t s = idx[k] - pads[k];
                const int64_t dim = x.shape[k];
                if (s < 0 || s >= dim)
                {
                    if (! reflect || dim < 2) { inside = false; break; }
                    const int64_t period = 2 * (dim - 1);
                    s = ((s % period) + period) % period;
                    if (s >= dim) s = period - s;
                }
                off += s * st[k];
            }
            out.f[e] = inside ? (float) getD (x, (size_t) off) : cv;
            for (int k = (int) rank - 1; k >= 0; --k) { if (++idx[(size_t) k] < shape[(size_t) k]) break; idx[(size_t) k] = 0; }
        }
        return true;
    }

    if (op == "Conv")
    {
        if (in.size() < 2 || in[1] == nullptr || x.shape.size() != 4 || in[1]->shape.size() != 4) { err = "only 2-D convolution"; return false; }
        const Tensor& w = *in[1];
        const Tensor* b = in.size() > 2 ? in[2] : nullptr;
        auto strides = attrInts ("strides"), dil = attrInts ("dilations"), pads = attrInts ("pads");
        if (strides.size() < 2) strides = { 1, 1 };
        if (dil.size() < 2) dil = { 1, 1 };
        if (pads.size() < 4) pads = { 0, 0, 0, 0 };
        const int64_t group = attrI ("group", 1);
        const int64_t N = x.shape[0], C = x.shape[1], H = x.shape[2], W = x.shape[3];
        const int64_t M = w.shape[0], Cg = w.shape[1], KH = w.shape[2], KW = w.shape[3];
        const auto autoPad = attrS ("auto_pad");
        if (autoPad == "SAME_UPPER" || autoPad == "SAME_LOWER")
        {
            const int64_t oh = (H + strides[0] - 1) / strides[0], ow = (W + strides[1] - 1) / strides[1];
            const int64_t ph = juce::jmax<int64_t> (0, (oh - 1) * strides[0] + (KH - 1) * dil[0] + 1 - H);
            const int64_t pw = juce::jmax<int64_t> (0, (ow - 1) * strides[1] + (KW - 1) * dil[1] + 1 - W);
            if (autoPad == "SAME_UPPER") pads = { ph / 2, pw / 2, ph - ph / 2, pw - pw / 2 };
            else pads = { ph - ph / 2, pw - pw / 2, ph / 2, pw / 2 };
        }
        const int64_t OH = (H + pads[0] + pads[2] - ((KH - 1) * dil[0] + 1)) / strides[0] + 1;
        const int64_t OW = (W + pads[1] + pads[3] - ((KW - 1) * dil[1] + 1)) / strides[1] + 1;
        if (OH <= 0 || OW <= 0 || C != Cg * group) { err = "bad convolution shape"; return false; }
        out = Tensor::floats ({ N, M, OH, OW });
        std::vector<float> xf;
        const float* xs = x.f.data();
        if (x.isInt) { xf.resize (x.size()); for (size_t k = 0; k < x.size(); ++k) xf[k] = (float) x.i[k]; xs = xf.data(); }
        const float* ws = w.f.data();
        const int64_t sy = strides[0], sx = strides[1], dy = dil[0], dx = dil[1];
        for (int64_t nb = 0; nb < N; ++nb)
            for (int64_t m = 0; m < M; ++m)
            {
                float* o = out.f.data() + ((nb * M + m) * OH) * OW;
                const float bias = b != nullptr && b->size() == (size_t) M ? b->f[(size_t) m] : 0.0f;
                for (int64_t q = 0; q < OH * OW; ++q) o[q] = bias;
                const int64_t g = m / (M / group);
                for (int64_t c = 0; c < Cg; ++c)
                {
                    const float* xc = xs + ((nb * C + g * Cg + c) * H) * W;
                    for (int64_t ky = 0; ky < KH; ++ky)
                        for (int64_t kx = 0; kx < KW; ++kx)
                        {
                            const float wv = ws[((m * Cg + c) * KH + ky) * KW + kx];
                            if (wv == 0.0f) continue;
                            // valid output columns for this tap: 0 <= ox*sx + kx*dx - padL < W
                            const int64_t xoff = kx * dx - pads[1];
                            if (W - 1 - xoff < 0) continue;
                            const int64_t ox0 = xoff >= 0 ? 0 : (-xoff + sx - 1) / sx;
                            const int64_t ox1 = juce::jmin (OW, (W - 1 - xoff) / sx + 1);
                            if (ox1 <= ox0) continue;
                            for (int64_t oy = 0; oy < OH; ++oy)
                            {
                                const int64_t iy = oy * sy + ky * dy - pads[0];
                                if (iy < 0 || iy >= H) continue;
                                const float* row = xc + iy * W + xoff;
                                float* orow = o + oy * OW;
                                if (sx == 1)
                                    for (int64_t ox = ox0; ox < ox1; ++ox) orow[ox] += wv * row[ox];
                                else
                                    for (int64_t ox = ox0; ox < ox1; ++ox) orow[ox] += wv * row[ox * sx];
                            }
                        }
                }
            }
        return true;
    }

    err = "unsupported operator";
    return false;
}

} // namespace snag::onnx
