// Proves the harness plumbing: two trivial cases, table output and JSON.
#include "bench.h"

#include <QtCore/QCoreApplication>

using namespace qce::bench;

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Runner runner;
    runner.add("noop/empty", [](Context &) {}, 20);
    runner.add("noop/xorshift_1M", [](Context &ctx) {
        ctx.setItems(1'000'000);
        // xorshift: a serial dependency the compiler cannot fold into a closed form
        quint64 x = 88172645463325252ull;
        for (int i = 0; i < 1'000'000; ++i) {
            x ^= x << 13;
            x ^= x >> 7;
            x ^= x << 17;
        }
        doNotOptimize(x);
    });
    return runner.exec(app.arguments());
}
