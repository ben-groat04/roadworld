#include "cp/edge_sampling.h"

#include <algorithm>
#include <cmath>

#include "cp/geom.h"

namespace cp {

double edge_heading(const Edge& e, double s) {
    double acc = 0;
    for (std::size_t i = 0; i < e.horizontal.spans.size(); ++i) {
        const Clothoid& c = e.horizontal.spans[i];
        if (s <= acc + c.length || i + 1 == e.horizontal.spans.size())
            return c.heading(std::clamp(s - acc, 0.0, c.length));
        acc += c.length;
    }
    return 0;
}

namespace {

struct SpanRef {
    const Clothoid* c;
    double s0;  // arc length at span start
};

std::vector<SpanRef> spans_of(const Edge& e) {
    std::vector<SpanRef> out;
    double acc = 0;
    for (const Clothoid& c : e.horizontal.spans) {
        out.push_back({&c, acc});
        acc += c.length;
    }
    return out;
}

const SpanRef& span_at(const std::vector<SpanRef>& spans, double s) {
    for (std::size_t i = 0; i + 1 < spans.size(); ++i)
        if (s < spans[i + 1].s0) return spans[i];
    return spans.back();
}

double curvature_at(const std::vector<SpanRef>& spans, double s) {
    const SpanRef& r = span_at(spans, s);
    return r.c->curvature(std::clamp(s - r.s0, 0.0, r.c->length));
}

// Offset curve curvature k / (1 - d k), guarded at the fold.
double offset_curvature(double k, double d) {
    const double f = 1 - d * k;
    if (std::abs(f) < kEps) return std::copysign(1e9, k);
    return k / f;
}

bool in_fold(const std::vector<FoldRange>& folds, double s) {
    for (const FoldRange& f : folds)
        if (s >= f.s0 - kEps && s <= f.s1 + kEps) return true;
    return false;
}

}  // namespace

double OffsetPolyline::length() const {
    double len = 0;
    for (std::size_t i = 1; i < samples.size(); ++i)
        len += (samples[i].pos.xy() - samples[i - 1].pos.xy()).length();
    return len;
}

std::vector<FoldRange> find_folds(const Edge& e, double offset) {
    std::vector<FoldRange> out;
    if (offset == 0) return out;
    for (const SpanRef& r : spans_of(e)) {
        // g(t) = a t + b >= 0 where a = d * dk, b = d * k0 - 1, t in [0, L].
        const double a = offset * r.c->dk;
        const double b = offset * r.c->k0 - 1;
        double t0 = 0, t1 = r.c->length;
        if (std::abs(a) < 1e-15) {
            if (b < 0) continue;
        } else {
            const double root = -b / a;
            if (a > 0) t0 = std::max(t0, root);
            else t1 = std::min(t1, root);
            if (t0 > t1) continue;
        }
        const FoldRange fr{r.s0 + t0, r.s0 + t1};
        if (!out.empty() && fr.s0 - out.back().s1 < kEps) out.back().s1 = fr.s1;
        else out.push_back(fr);
    }
    return out;
}

OffsetPolyline sample_offset(const Edge& e, double d, const SamplingParams& p) {
    OffsetPolyline out;
    out.offset = d;
    const auto spans = spans_of(e);
    if (spans.empty()) return out;
    const double total = e.horizontal.length();
    out.folds = find_folds(e, d);

    // Breakpoints: span boundaries and fold ends, so curvature is linear between them.
    std::vector<double> bp{0.0, total};
    for (const SpanRef& r : spans) bp.push_back(r.s0);
    for (const FoldRange& f : out.folds) {
        bp.push_back(f.s0);
        bp.push_back(f.s1);
    }
    std::sort(bp.begin(), bp.end());
    std::vector<double> cuts;
    for (double s : bp) {
        s = std::clamp(s, 0.0, total);
        if (cuts.empty() || s - cuts.back() > kEps) cuts.push_back(s);
    }

    auto emit = [&](double s) {
        const SpanRef& r = span_at(spans, s);
        const double t = std::clamp(s - r.s0, 0.0, r.c->length);
        const Pose pose = r.c->pose(t);
        const double k = r.c->curvature(t);
        EdgeSample smp;
        smp.s = s;
        smp.heading = pose.heading;
        smp.pos = {pose.pos.x - d * std::sin(pose.heading), pose.pos.y + d * std::cos(pose.heading),
                   e.vertical.z(s)};
        smp.curvature = d == 0 ? k : offset_curvature(k, d);
        smp.grade = e.vertical.grade(s);
        smp.folded = d != 0 && in_fold(out.folds, s);
        out.samples.push_back(smp);
    };

    // True if a step [s, s + ds] meets the chord, heading and fold-approach limits.
    auto step_ok = [&](double s, double ds) {
        double kmax = 0, fmin = 1e9, chord = 0;
        for (double q : {s, s + 0.5 * ds, s + ds}) {
            const double k = curvature_at(spans, q);
            const double f = 1 - d * k;
            kmax = std::max(kmax, std::abs(k));
            if (f > 0) {
                fmin = std::min(fmin, f);
                chord = std::max(chord, std::abs(k) * f);  // offset sagitta = |k| f ds^2 / 8
            } else {
                chord = std::max(chord, std::abs(k));
            }
        }
        if (kmax * ds > p.max_heading_change) return false;
        if (chord * ds * ds / 8 > p.max_chord_error) return false;
        // Refine as the offset approaches a fold (offset * curvature -> 1).
        if (fmin < 1 && ds > p.max_step * std::max(fmin, 0.1)) return false;
        return true;
    };

    emit(cuts.front());
    for (std::size_t i = 0; i + 1 < cuts.size(); ++i) {
        double s = cuts[i];
        const double end = cuts[i + 1];
        while (end - s > kEps) {
            double ds = std::min(p.max_step, end - s);
            while (ds > 1e-4 && !step_ok(s, ds)) ds *= 0.5;
            // Avoid a sliver at the end of the segment.
            if (end - (s + ds) < 0.5 * ds) ds = end - s;
            s = (end - (s + ds) < kEps) ? end : s + ds;
            emit(s);
        }
    }
    return out;
}

std::vector<EdgeSample> sample_centreline(const Edge& e, const SamplingParams& p) {
    return sample_offset(e, 0.0, p).samples;
}

}  // namespace cp
