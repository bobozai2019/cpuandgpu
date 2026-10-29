#include "engine.h"
#include <algorithm>
#include <iostream>
#include <numeric>
#include <stdexcept>

static void Check(bool condition, const char* reason) { if (!condition) throw std::runtime_error(reason); }
int main() {
    using namespace lab;
    try {
        Config c; c.width = 73; c.height = 41; c.samples = 3; c.tile = 16;
        Engine pool(7);
        c.mode = Mode::Serial; pool.Start(c); const auto reference = pool.Finish();
        Check(reference.checksum != 0, "empty reference");
        // Independent tile coverage audit, odd dimensions, uneven splits, repeated pool reuse.
        for (auto mode : {Mode::Serial,Mode::Static,Mode::Dynamic}) {
            for (int tile : {4,16,64,128}) {
                c.mode = mode; c.tile = tile; pool.Start(c);
                // Poll and progressive image exercise the release/acquire publication path.
                while (pool.Poll().running) { auto preview = pool.Image(); Check(preview.size()==73*41, "preview size"); std::this_thread::yield(); }
                auto r = pool.Finish();
                Check(r.pixels == reference.pixels, "schedule or tile size changed pixels");
                Check(r.snapshot.completed == r.snapshot.total, "missing tile");
                Check(!r.snapshot.cancelled, "unexpected cancellation");
                std::vector<int> coverage(73*41);
                for (const auto& t : r.snapshot.tiles) {
                    Check(t.state == 2 && t.owner >= 0 && t.owner < 7, "invalid owner/state");
                    Check(t.end >= t.begin && t.end <= r.snapshot.wallMs, "invalid timing");
                    for (int y=t.y; y<t.y+t.h; ++y) for (int x=t.x; x<t.x+t.w; ++x) ++coverage[size_t(y)*73+x];
                }
                Check(std::all_of(coverage.begin(),coverage.end(),[](int n){return n==1;}), "overlap or gap");
                int count=0; for (auto s : r.snapshot.stats) count+=s.tiles;
                Check(count==r.snapshot.total, "worker accounting");
            }
        }
        Config large; large.samples=256; pool.Start(large);
        bool rejected=false; try {pool.Start(large);} catch(const std::logic_error&){rejected=true;}
        Check(rejected,"concurrent Start accepted");
        pool.Cancel(); auto cancelled=pool.Finish();
        Check(cancelled.snapshot.cancelled,"cancel not recorded");
        c.mode=Mode::Dynamic; pool.Start(c); Check(pool.Finish().pixels==reference.pixels,"reuse after cancel");
        { Engine destructWhileRunning(3); destructWhileRunning.Start(large); }
        bool invalid=false; try {Engine zero(0);} catch(const std::invalid_argument&){invalid=true;}
        Check(invalid,"zero workers accepted");
        invalid=false; try {c.tile=0; pool.Start(c);} catch(const std::invalid_argument&){invalid=true;}
        Check(invalid,"zero tile accepted");
        std::cout << "PASS: exact pixels, edge tiles, disjoint coverage, workers > tiles, accounting, publication, restart, cancel, active destruction, invalid inputs\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
