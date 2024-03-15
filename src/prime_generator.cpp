// Source: https://www.geeksforgeeks.org/sieve-of-eratosthenes/
// C++ program to print all primes smaller than or equal to
// n using Sieve of Eratosthenes

// Improved by Dan Strano of Unitary Fund, 2024.
// We can think of trial division as exact inverse of
// Sieve of Eratosthenes, with log space and log time.
// The modular division part is a costly atomic operation.
// It need only be carried out up the square root of the
// number under trial. Multiples of 2, 3, and 5 can be
// entirely skipped in loop enumeration.

#include "config.h"

#include <iostream>
#include <vector>

#if USE_GMP
#include <boost/multiprecision/gmp.hpp>
#elif USE_BOOST
#include <boost/multiprecision/cpp_int.hpp>
#else
#include "big_integer.hpp"
#endif

#if BIG_INT_BITS < 33
typedef uint32_t BigInteger;
#elif BIG_INT_BITS < 65
typedef uint64_t BigInteger;
#else
#if USE_GMP
typedef boost::multiprecision::mpz_int BigInteger;
#elif USE_BOOST
typedef boost::multiprecision::number<boost::multiprecision::cpp_int_backend<BIG_INT_BITS, BIG_INT_BITS,
    boost::multiprecision::unsigned_magnitude, boost::multiprecision::unchecked, void>>
    BigInteger;
#else
typedef BigInteger BigInteger;
#endif
#endif

inline BigInteger sqrt(const BigInteger& toTest)
{
    // Otherwise, find b = sqrt(b^2).
    BigInteger start = 1U, end = toTest >> 1U, ans = 0U;
    do {
        const BigInteger mid = (start + end) >> 1U;

        // If toTest is a perfect square
        const BigInteger sqr = mid * mid;
        if (sqr == toTest) {
            ans = mid;
            break;
        }

        if (sqr < toTest) {
            // Since we need floor, we update answer when mid*mid is smaller than p, and move closer to sqrt(p).
            start = mid + 1U;
            ans = mid;
        } else {
            // If mid*mid is greater than p
            end = mid - 1U;
        }
    } while (start <= end);

    return ans;
}

BigInteger backward(BigInteger ni) {
    ni = (ni + 1) >> 1;
    ni = ((ni + 1) << 1) / 3;
    return ni;
}

BigInteger forward(BigInteger p) {
    // Make this NOT a multiple of 2 or 3.
    p += (p >> 1U);
    return (p << 1U) - 1U;
}

bool isTimeOrSpaceMultiple(BigInteger p, const std::vector<BigInteger>& knownPrimes) {
    const BigInteger sqrtP = sqrt(p);
    for (BigInteger i : knownPrimes) {
        if (i > sqrtP) {
            return false;
        }
        if ((p % i) == 0) {
            return true;
        }
    }
    return false;
}

bool isTimeMultiple(BigInteger p, const std::vector<BigInteger>& knownPrimes) {
    const BigInteger sqrtP = sqrt(p);
    for (size_t i = 2U; i < knownPrimes.size(); ++i) {
        if (i > sqrtP) {
            return false;
        }
        if ((p % knownPrimes[i]) == 0) {
            return true;
        }
    }
    return false;
}

std::vector<BigInteger> TrialDivision(const BigInteger& n)
{
    std::vector<BigInteger> knownPrimes = { 2, 3, 5 };

    if (n < 2) {
        return std::vector<BigInteger>();
    }
    if (n < 3) {
        return std::vector<BigInteger>(knownPrimes.begin(), knownPrimes.begin() + 1);
    }
    if (n < 5) {
        return std::vector<BigInteger>(knownPrimes.begin(), knownPrimes.begin() + 2);
    }
    if (n < 7) {
        return std::vector<BigInteger>(knownPrimes.begin(), knownPrimes.begin() + 3);
    }

    // We are excluding multiples of the first few
    // small primes from outset. For multiples of
    // 2 and 3, this reduces complexity by 2/3.
    // const BigInteger cardinality = (~((~n) | 1)) / 3;

    // Get the remaining prime numbers.
    BigInteger o = 2;
    bool isWorking = true;
    while (isWorking) {
        for (int i = 1; i < 7; ++i) {
            BigInteger p = forward(o + i);

            if (p > n) {
                isWorking = false;
                break;
            }

            if (isTimeMultiple(p, knownPrimes)) {
                // Skip
                continue;
            }

            knownPrimes.push_back(p);
        }

        for (int i = 8; i < 10; ++i) {
            BigInteger p = forward(o + i);

            if (p > n) {
                isWorking = false;
                break;
            }

            if (isTimeMultiple(p, knownPrimes)) {
                // Skip
                continue;
            }

            knownPrimes.push_back(p);
        }

        o = o + 10;
    }

    return knownPrimes;
}
 
// Driver Code
int main()
{
    BigInteger n;

    std::cout << "Primes up to number: ";
    std::cin >> n;

    std:: cout << "Following are the prime numbers smaller than or equal to " << n << ":" << std::endl;

    const std::vector<BigInteger> primes = TrialDivision(n);

    for (BigInteger p : primes) {
        std::cout << p << " ";
    }
    std::cout << std::endl;

    return 0;
}
