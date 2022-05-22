////////////////////////////////////////////////////////////////////////////////////////////////
//
// (C) Daniel Strano and the Qrack contributors 2017-2022. All rights reserved.
//
// "A quantum-inspired Monte Carlo integer factoring algorithm"
//
// This example demonstrates a (Shor's-like) "quantum-inspired" algorithm for integer factoring.
// This approach is similar to Shor's algorithm, except with a uniformly random output from the
// quantum period-finding subroutine. Therefore, we don't need quantum computer simulation for
// this algorithm at all!
//
// (This file was heavily adapted from
// https://github.com/ProjectQ-Framework/ProjectQ/blob/develop/examples/shor.py,
// with thanks to ProjectQ!)
//
// Licensed under the GNU Lesser General Public License V3.
// See LICENSE.md in the project root or https://www.gnu.org/licenses/lgpl-3.0.en.html
// for details.

#include <chrono>
#include <cmath>
#include <iomanip> // For setw
#include <iostream> // For cout
#include <random>
#include <stdlib.h>
#include <time.h>

#include <atomic>
#include <future>
#include <mutex>

#define ONE_BCI ((bitCapInt)1UL)
// Change QBCAPPOW, if you need more than 2^12 bits of factorized integer, within Boost and system limits.
#define QBCAPPOW 12U

#if QBCAPPOW < 8U
#define bitLenInt uint8_t
#elif QBCAPPOW < 16U
#define bitLenInt uint16_t
#elif QBCAPPOW < 32U
#define bitLenInt uint32_t
#else
#define bitLenInt uint64_t
#endif

#if QBCAPPOW < 6U
#define bitsInCap 32
#define bitCapInt uint32_t
#elif QBCAPPOW < 7U
#define bitsInCap 64
#define bitCapInt uint64_t
#elif QBCAPPOW < 8U
#define bitsInCap 128
#ifdef BOOST_AVAILABLE
#include <boost/multiprecision/cpp_int.hpp>
#define bitCapInt boost::multiprecision::uint128_t
#else
#define bitCapInt __uint128_t
#endif
#else
#define bitsInCap (8U * (((bitLenInt)1U) << QBCAPPOW))
#include <boost/multiprecision/cpp_int.hpp>
#define bitCapInt                                                                                                      \
    boost::multiprecision::number<boost::multiprecision::cpp_int_backend<1 << QBCAPPOW, 1 << QBCAPPOW,                 \
        boost::multiprecision::unsigned_magnitude, boost::multiprecision::unchecked, void>>
#endif

// Source: https://www.exploringbinary.com/ten-ways-to-check-if-an-integer-is-a-power-of-two-in-c/
inline bool isPowerOfTwo(const bitCapInt& x) { return (x && !(x & (x - ONE_BCI))); }

bitLenInt log2(const bitCapInt& n)
{
    bitLenInt pow = 0;
    bitCapInt p = n >> 1U;
    while (p != 0) {
        p >>= 1U;
        pow++;
    }
    return pow;
}

// Source:
// https://stackoverflow.com/questions/101439/the-most-efficient-way-to-implement-an-integer-based-power-function-powint-int#answer-101613
bitCapInt uipow(const bitCapInt& base, const bitCapInt& exp)
{
    bitCapInt result = 1U;
    bitCapInt b = base;
    bitCapInt e = exp;
    for (;;) {
        if (b & 1U) {
            result *= b;
        }
        e >>= 1U;
        if (!e) {
            break;
        }
        b *= b;
    }

    return result;
}

// It's fine if this is not exact for the whole bitCapInt domain, so long as it is <= the exact result.
bitCapInt intLog(const bitCapInt& base, const bitCapInt& arg)
{
    bitCapInt x = arg;
    bitCapInt result = 0U;
    while (x >= base) {
        x /= base;
        result++;
    }
    return result;
}

bitCapInt gcd(const bitCapInt& n1, const bitCapInt& n2)
{
    if (n2 == 0)
        return n1;
    return gcd(n2, n1 % n2);
}

bitCapInt continued_fraction_step(bitCapInt* numerator, bitCapInt* denominator)
{
    bitCapInt intPart = (*numerator) / (*denominator);
    bitCapInt partDenominator = (*numerator) - intPart * (*denominator);
    bitCapInt partNumerator = (*denominator);

    (*numerator) = partNumerator;
    (*denominator) = partDenominator;
    return intPart;
}

void calc_continued_fraction(std::vector<bitCapInt> denominators, bitCapInt* numerator, bitCapInt* denominator)
{
    bitCapInt approxNumer = 1U;
    bitCapInt approxDenom = denominators.back();
    bitCapInt temp;

    for (int i = (denominators.size() - 1); i > 0; i--) {
        temp = denominators[i] * approxDenom + approxNumer;
        approxNumer = approxDenom;
        approxDenom = temp;
    }

    (*numerator) = approxNumer;
    (*denominator) = approxDenom;
    // return ((double)approxNumer) / ((double)approxDenom);
}

int main()
{
    typedef std::uniform_int_distribution<uint64_t> rand_dist;

    bitCapInt toFactor;

    std::cout << "Number to factor: ";
    std::cin >> toFactor;

    const double clockFactor = 1.0 / 1000.0; // Report in ms
    auto iterClock = std::chrono::high_resolution_clock::now();

    const bitLenInt qubitCount = log2(toFactor) + (!isPowerOfTwo(toFactor) ? 1U : 0U);
    const bitCapInt qubitPower = ONE_BCI << qubitCount;
    std::cout << "Bits to factor: " << (int)qubitCount << std::endl;

    const bitLenInt wordSize = 64U;
    const bitCapInt maxPow = ONE_BCI << wordSize;
    std::vector<rand_dist> toFactorDist;
    bitCapInt distPart = toFactor - 2U;
    while (distPart) {
        toFactorDist.push_back(rand_dist(0U, (uint64_t)(distPart % maxPow)));
        distPart >>= wordSize;
    }
    std::reverse(toFactorDist.begin(), toFactorDist.end());

    std::random_device rand_dev;
    std::mt19937 rand_gen(rand_dev());

    const unsigned threads = std::thread::hardware_concurrency();
    std::atomic<bool> isFinished;
    isFinished = false;

    std::vector<std::future<void>> futures(threads);
    for (unsigned cpu = 0U; cpu < threads; cpu++) {
        futures[cpu] = std::async(std::launch::async, [&] {
            const uint64_t BATCH_SIZE = 1U << 9U;

            for (;;) {
                for (uint64_t batchItem = 0U; batchItem < BATCH_SIZE; batchItem++) {
                    // Choose a base at random, >1 and <toFactor.
                    // Construct a random number
                    bitCapInt base = toFactorDist[0](rand_gen);
                    for (size_t i = 1U; i < toFactorDist.size(); i++) {
                        base <<= wordSize;
                        base |= toFactorDist[i](rand_gen);
                    }
                    base += 2U;

                    bitCapInt testFactor = gcd(toFactor, base);
                    if (testFactor != 1) {
                        std::cout << "Chose non- relative prime: " << testFactor << " * " << (toFactor / testFactor)
                                  << std::endl;
                        auto tClock = std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::high_resolution_clock::now() - iterClock);
                        std::cout << "(Time elapsed: " << (tClock.count() * clockFactor) << "ms)" << std::endl;
                        std::cout << "(Waiting to join other threads...)" << std::endl;
                        return;
                    }

                    // This would be where we perform the quantum period finding algorithm.
                    // However, we don't have a quantum computer!
                    // Instead, we "throw dice" for a guess to the output of the quantum subroutine.
                    // This guess will usually be wrong, at least for semi-prime inputs.
                    // If we try many times, though, this can be a practically valuable factoring method.

                    // Firstly, the period of ((base ^ x) MOD toFactor) can't be smaller than log_base(toFactor).
                    const bitCapInt minR = intLog(base, toFactor);
                    // y is meant to be close to some number c * qubitPower / r, where "r" is the period.
                    // c is a positive integer or 0, and we don't want the 0 case.
                    // y is truncated by the number of qubits in the register, at most.
                    // The maximum value of c before truncation is no higher than r.
                    // Based on the above, y is between minR and qubitPower.

                    // Consider c and r to be two independent numbers, bounded by any considerations including above.
                    // (They probably shouldn't have the same range limits for RNG. This is being considered.)
                    const bitCapInt yRange = qubitPower - minR;
                    bitCapInt yPart = yRange;
                    bitCapInt rGuess = 0;
                    bitCapInt c = 0;
                    while (yPart) {
                        rand_dist yDist(0, (uint64_t)(yPart % maxPow));
                        yPart >>= wordSize;
                        rGuess <<= wordSize;
                        rGuess |= yDist(rand_gen);
                        c <<= wordSize;
                        c |= yDist(rand_gen);
                    }
                    rGuess += minR;
                    c++;
                    const bitCapInt y = (c * qubitPower) / rGuess;

                    // Value is always fractional, so skip first step, by flipping numerator and denominator:
                    bitCapInt numerator = qubitPower;
                    bitCapInt denominator = y;

                    std::vector<bitCapInt> denominators;
                    bitCapInt approxNumer;
                    bitCapInt approxDenom;
                    do {
                        denominators.push_back(continued_fraction_step(&numerator, &denominator));
                        calc_continued_fraction(denominators, &approxNumer, &approxDenom);
                    } while ((denominator > 0) && (approxDenom < toFactor));
                    denominators.pop_back();

                    bitCapInt r;
                    if (denominators.size() == 0) {
                        r = y;
                    } else {
                        calc_continued_fraction(denominators, &approxNumer, &r);
                    }

                    // Try to determine the factors
                    if (r & 1U) {
                        r <<= 1U;
                    }
                    const bitCapInt p = r >> 1U;
                    const bitCapInt apowrhalf = uipow(base, p) % toFactor;
                    bitCapInt f1 = (bitCapInt)gcd(apowrhalf + 1U, toFactor);
                    bitCapInt f2 = (bitCapInt)gcd(apowrhalf - 1U, toFactor);
                    bitCapInt fmul = f1 * f2;
                    while (((f1 * f2) != toFactor) && ((f1 * f2) > 1U) && ((toFactor / fmul) * fmul == toFactor)) {
                        fmul = f1;
                        f1 = fmul * f2;
                        f2 = toFactor / (fmul * f2);
                        fmul = f1 * f2;
                    }
                    if (((f1 * f2) == toFactor) && (f1 > 1U) && (f2 > 1U)) {
                        std::cout << "Success: Found " << f1 << " * " << f2 << " = " << toFactor << std::endl;
                        auto tClock = std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::high_resolution_clock::now() - iterClock);
                        std::cout << "(Time elapsed: " << (tClock.count() * clockFactor) << "ms)" << std::endl;
                        std::cout << "(Waiting to join other threads...)" << std::endl;
                        isFinished = true;
                        return;
                    } // else {
                      // std::cout << "Failure: Found " << res1 << " and " << res2 << std::endl;
                    // }
                }

                // Check if finished, between batches.
                if (isFinished) {
                    break;
                }
            }
        });
    };

    for (unsigned cpu = 0U; cpu < threads; cpu++) {
        futures[cpu].get();
    }

    return 0;
}
