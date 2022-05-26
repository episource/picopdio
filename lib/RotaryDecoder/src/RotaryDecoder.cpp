#include "RotaryDecoder.h"

enum RotaryDecoder::State : uint8_t {
    DETENT_00,
    DETENT_11,
    HALF_FORWARD_01,
    HALF_FORWARD_10,
    HALF_BACKWARD_01,
    HALF_BACKWARD_10,
    FORWARD_00,
    FORWARD_11,
    BACKWARD_00,
    BACKWARD_11,
    DB_WAS_HALF_FORWARD_01,
    DB_WAS_HALF_FORWARD_10,
    DB_WAS_HALF_BACKWARD_01,
    DB_WAS_HALF_BACKWARD_10
};

const RotaryDecoder::State RotaryDecoder::TRANSITION_LUT[14][4] {
        /* State                      | Transition 00 | Transition 01          | Transition 10           | Transition 11 */
        /* DETENT_00 */               { DETENT_00     , HALF_FORWARD_01        , HALF_BACKWARD_10        , DETENT_11     },
        /* DETENT_11 */               { DETENT_00     , HALF_BACKWARD_01       , HALF_FORWARD_10         , DETENT_11     },
        /* HALF_FORWARD_01 */         { DETENT_00     , HALF_FORWARD_01        , DB_WAS_HALF_FORWARD_01  , FORWARD_11    },
        /* HALF_FORWARD_10 */         { FORWARD_00    , DB_WAS_HALF_FORWARD_10 , HALF_FORWARD_10         , DETENT_11     },
        /* HALF_BACKWARD_01 */        { BACKWARD_00   , HALF_BACKWARD_01       , DB_WAS_HALF_BACKWARD_01 , DETENT_11     },
        /* HALF_BACKWARD_10 */        { DETENT_00     , DB_WAS_HALF_BACKWARD_10, HALF_BACKWARD_10        , BACKWARD_11   },
        /* FORWARD_00 */              { DETENT_00     , HALF_FORWARD_01        , HALF_BACKWARD_10        , DETENT_11     },
        /* FORWARD_11 */              { DETENT_00     , HALF_BACKWARD_01       , HALF_FORWARD_01         , DETENT_11     },
        /* BACKWARD_00 */             { DETENT_00     , HALF_FORWARD_01        , HALF_BACKWARD_10        , DETENT_11     },
        /* BACKWARD_11 */             { DETENT_00     , HALF_BACKWARD_01       , HALF_FORWARD_10         , DETENT_11     },
        /* DB_WAS_HALF_FORWARD_01 */  { DETENT_00     , HALF_FORWARD_01        , DB_WAS_HALF_FORWARD_01  , FORWARD_11    },
        /* DB_WAS_HALF_FORWARD_10 */  { FORWARD_00    , DB_WAS_HALF_FORWARD_10 , HALF_FORWARD_10         , DETENT_11     },
        /* DB_WAS_HALF_BACKWARD_01 */ { BACKWARD_00   , HALF_BACKWARD_01       , DB_WAS_HALF_BACKWARD_01 , DETENT_11     },
        /* DB_WAS_HALF_BACKWARD_10 */ { DETENT_00     , DB_WAS_HALF_BACKWARD_10, HALF_BACKWARD_10        , BACKWARD_11   }
};

RotaryDecoder::Step RotaryDecoder::decode(bool highPinVal, bool lowPinVal) {
    _s = TRANSITION_LUT[_s][highPinVal << 1 | lowPinVal];

    switch (_s) {
        case FORWARD_00:
        case FORWARD_11:
            return Step::FORWARD;
        case BACKWARD_00:
        case BACKWARD_11:
            return Step::BACKWARD;
        default:
            return Step::NO_STEP;
    }
}

RotaryDecoder::Step RotaryDecoder::decode(uint8_t highPinVal, uint8_t lowPinVal) {
    return decode(highPinVal == 1, lowPinVal == 1);
}

RotaryDecoder::Step RotaryDecoder::decode(PinStatus highPinVal, PinStatus lowPinVal) {
    return decode(highPinVal == PinStatus::HIGH, lowPinVal == PinStatus::HIGH);
}

RotaryDecoder::Step RotaryDecoder::readAndDecode(uint8_t highPin, uint8_t lowPin) {
    return decode(digitalRead(highPin), digitalRead(lowPin));
}
