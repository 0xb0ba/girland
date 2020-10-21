#ifndef __INC_MINILED_H
#define __INC_MINILED_H

#include "miniled_tools.h"
#include "miniled_send.h"

#ifndef COLOR_DEBTH
#define COLOR_DEBTH 3  // по умолчанию 24 бита
#endif

struct TLED {
#if (COLOR_DEBTH == 1)
  uint8_t data;
  TLED& set_r(uint8_t ir) {
    data = (data & 0b00111111) | ((ir) & 0b11000000);
    return *this;
  }
  TLED& set_g(uint8_t ig) {
    data = (data & 0b11000111) | ((ig >> 2) & 0b00111000);
    return *this;
  }
  TLED& set_b(uint8_t ib) {
    data = (data & 0b11111000) | ((ib >> 5) & 0b00000111);
    return *this;
  }
  uint8_t get_r() {
    return (data & 0b11000000);
  }
  uint8_t get_g() {
    return (data & 0b00111000) << 2;
  }
  uint8_t get_b() {
    return (data & 0b00000111) << 5;
  }
#elif (COLOR_DEBTH == 2)
  uint16_t data;
  TLED& set_r(uint8_t ir) {
    data = (data & 0b0000011111111111) | ((ir << 8) & 0b1111100000000000);
    return *this;
  }
  TLED& set_g(uint8_t ig) {
    data = (data & 0b1111100000011111) | ((ig << 3) & 0b0000011111100000);
    return *this;
  }
  TLED& set_b(uint8_t ib) {
    data = (data & 0b1111111111100000) | ((ib >> 3) & 0b0000000000011111);
    return *this;
  }
  uint8_t get_r() {
    return (data & 0b1111100000000000) >> 8;
  }
  uint8_t get_g() {
    return (data & 0b0000011111100000) >> 3;
  }
  uint8_t get_b() {
    return (data & 0b0000000000011111) << 3;
  }
#elif (COLOR_DEBTH == 3)
  union {
    struct {
      uint8_t gg;
      uint8_t rr;
      uint8_t bb;
    };
    uint8_t data[3];
  };

  TLED& set_r(uint8_t ir) {
    rr = ir;
    return *this;
  }
  TLED& set_g(uint8_t ig) {
    gg = ig;
    return *this;
  }
  TLED& set_b(uint8_t ib) {
    bb = ib;
    return *this;
  }
  uint8_t get_r() {
    return rr;
  }
  uint8_t get_g() {
    return gg;
  }
  uint8_t get_b() {
    return bb;
  }
#endif //#elif (COLOR_DEBTH == 3)

  TLED() {}

  TLED(uint8_t ir, uint8_t ig, uint8_t ib) {
    set_r(ir);
    set_g(ig);
    set_b(ib);
  }

  //  TLED(const TLED& led) {
  //    set_r(led.get_r());
  //    set_g(led.get_g());
  //    set_b(led.get_g());
  //  }

  TLED& operator=(const uint32_t colorcode) {
    set_r((colorcode >> 16) & 0xFF);
    set_g((colorcode >>  8) & 0xFF);
    set_b((colorcode >>  0) & 0xFF);
    return *this;
  }

  //  TLED& operator=(const TLED& led) {
  //    set_r(led.get_r());
  //    set_g(led.get_g());
  //    set_b(led.get_g());
  //    return *this;
  //  }

  TLED& nscale8(uint8_t scale) {
    set_r(((int)get_r() * (int)scale) >> 8);
    set_g(((int)get_g() * (int)scale) >> 8);
    set_b(((int)get_b() * (int)scale) >> 8);
    return *this;
  }
};

TLED operator/(const TLED& led, uint8_t d) {
  return TLED(led.get_r() / d, led.get_g() / d, led.get_b() / d);
};

TLED operator+(const TLED& led1, const TLED& led2) {
  return TLED(qadd8(led1.get_r(), led2.get_r()),
              qadd8(led1.get_g(), led2.get_g()),
              qadd8(led1.get_b(), led2.get_b())
             );
};

class TStrip {
    TLED *LEDbuffer;
    int _numLEDs;
    uint8_t _bright = 0;

    const volatile uint8_t *ws2812_port;
    volatile uint8_t *ws2812_port_reg;
    uint8_t pinMask;

  public:
    TStrip(TLED *buffer, int count, uint8_t pin) {
      _numLEDs = count;
      LEDbuffer = buffer;

      pinMask = digitalPinToBitMask(pin);
      ws2812_port = portOutputRegister(digitalPinToPort(pin));
      ws2812_port_reg = portModeRegister(digitalPinToPort(pin));
    }

    void show() {
      *ws2812_port_reg |= pinMask; // Enable DDR
      WS2812B_sendData((PTR_TYPE)LEDbuffer, (int16_t)COLOR_DEBTH * _numLEDs, pinMask, (uint8_t*) ws2812_port, (uint8_t*) ws2812_port_reg, _bright);
    };

    void setBrightness(uint8_t br) {
      _bright = br;
    };
};

TLED CHSV(uint8_t hue, uint8_t saturation, uint8_t value) {
  TLED rgb;

  hue = scale8( hue, 191);

  // Saturation more useful the other way around
  saturation = 255 - saturation;
  uint8_t invsat = APPLY_DIMMING( saturation );

  // Apply dimming curves
  value = APPLY_DIMMING( value );

  // The brightness floor is minimum number that all of
  // R, G, and B will be set to, which is value * invsat
  uint8_t brightness_floor;

  asm volatile(
    "mul %[value], %[invsat]            \n"
    "mov %[brightness_floor], r1        \n"
    : [brightness_floor] "=r" (brightness_floor)
    : [value] "r" (value),
    [invsat] "r" (invsat)
    : "r0", "r1"
  );

  // The color amplitude is the maximum amount of R, G, and B
  // that will be added on top of the brightness_floor to
  // create the specific hue desired.
  uint8_t color_amplitude = value - brightness_floor;

  // Figure how far we are offset into the section of the
  // color wheel that we're in
  uint8_t offset = hue & (HSV_SECTION_3 - 1);  // 0..63
  uint8_t rampup = offset * 4; // 0..252


  // compute color-amplitude-scaled-down versions of rampup and rampdown
  uint8_t rampup_amp_adj;
  uint8_t rampdown_amp_adj;

  asm volatile(
    "mul %[rampup], %[color_amplitude]       \n"
    "mov %[rampup_amp_adj], r1               \n"
    "com %[rampup]                           \n"
    "mul %[rampup], %[color_amplitude]       \n"
    "mov %[rampdown_amp_adj], r1             \n"
    : [rampup_amp_adj] "=&r" (rampup_amp_adj),
    [rampdown_amp_adj] "=&r" (rampdown_amp_adj),
    [rampup] "+r" (rampup)
    : [color_amplitude] "r" (color_amplitude)
    : "r0", "r1"
  );

  // add brightness_floor offset to everything
  uint8_t rampup_adj_with_floor   = rampup_amp_adj   + brightness_floor;
  uint8_t rampdown_adj_with_floor = rampdown_amp_adj + brightness_floor;


  // keep gcc from using "X" as the index register for storing
  // results back in the return structure.  AVR's X register can't
  // do "std X+q, rnn", but the Y and Z registers can.
  // if the pointer to 'rgb' is in X, gcc will add all kinds of crazy
  // extra instructions.  Simply killing X here seems to help it
  // try Y or Z first.
  asm volatile(  ""  :  :  : "r26", "r27" );


  if ( hue & 0x80 ) {
    // section 2: 0x80..0xBF
    rgb = TLED(rampup_adj_with_floor, brightness_floor, rampdown_adj_with_floor);
  } else {
    if ( hue & 0x40) {
      // section 1: 0x40..0x7F
      rgb =  TLED(brightness_floor, rampdown_adj_with_floor, rampup_adj_with_floor);
    } else {
      // section 0: 0x00..0x3F
      rgb = TLED( rampdown_adj_with_floor, rampup_adj_with_floor, brightness_floor);
    }
  }

  //    cleanup_R1();
  asm volatile( "clr __zero_reg__  \n\t" : : : "r1" );

  return rgb;
}
// End of AVR asm implementation

#endif  //#ifndef __INC_MINILED_H
