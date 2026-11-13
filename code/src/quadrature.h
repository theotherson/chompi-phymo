/** @file quadrature.h
 *  @brief Encoder pins to detents, without bounce.
 *
 *  Follows the two pins through their four states and sums the steps, so a
 *  bounce (a step and straight back) adds nothing. A detent counts when the
 *  pins return to rest with at least two steps one way. Each state must be
 *  read twice in a row to count.
 */
#pragma once
#include <cstdint>

namespace chompi
{

/** [from][to]: +1 a step clockwise (3 > 2 > 0 > 1 > 3), -1 back, 0 none
 *  or a missed state. */
constexpr int8_t kQuadratureStep[4][4] = {
    {0, 1, -1, 0},  // from 00
    {-1, 0, 0, 1},  // from 01
    {1, 0, 0, -1},  // from 10
    {0, -1, 1, 0}}; // from 11

class Quadrature
{
  public:
    /** One reading of the pins (true = high). @return +1 a detent clockwise,
     *  -1 one anticlockwise, 0 nothing yet. */
    int Step(bool a, bool b)
    {
        const uint8_t s = static_cast<uint8_t>((a ? 2 : 0) | (b ? 1 : 0));
        if(s != pending_) // seen once: wait for a second read
        {
            pending_ = s;
            return 0;
        }
        if(s == state_)
            return 0;
        count_ += kQuadratureStep[state_][s];
        state_ = s;
        if(state_ != kRest)
            return 0;
        const int detent = count_ >= 2 ? 1 : (count_ <= -2 ? -1 : 0);
        count_           = 0;
        return detent;
    }

  private:
    static constexpr uint8_t kRest = 3; // both pins high

    uint8_t state_ = kRest, pending_ = kRest;
    int8_t  count_ = 0;
};

} // namespace chompi
