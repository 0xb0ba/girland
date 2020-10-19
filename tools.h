#ifndef __INC_TOOLS_H
#define __INC_TOOLS_H

#ifndef FASTLED_VERSION

struct CRGB;
struct CHSV;

uint8_t qadd8( uint8_t a, uint8_t b) {
  unsigned int t = a + b;
  if ( t > 255) t = 255;
  return t;
}

typedef uint8_t   fract8;

uint8_t scale8( uint8_t i, fract8 scale) {
  asm volatile(
    /* Multiply 8-bit i * 8-bit scale, giving 16-bit r1,r0 */
    "mul %0, %1          \n\t"
    /* Move the high 8-bits of the product (r1) back to i */
    "mov %0, r1          \n\t"
    /* Restore r1 to "0"; it's expected to always be that */
    "clr __zero_reg__    \n\t"

    : "+a" (i)      /* writes to i */
    : "a"  (scale)  /* uses scale */
    : "r0", "r1"    /* clobbers r0, r1 */ );

  /* Return the result */
  return i;
}

uint8_t scale8_LEAVING_R1_DIRTY( uint8_t i, fract8 scale) {
  asm volatile(
#if (FASTLED_SCALE8_FIXED==1)
    // Multiply 8-bit i * 8-bit scale, giving 16-bit r1,r0
    "mul %0, %1          \n\t"
    // Add i to r0, possibly setting the carry flag
    "add r0, %0         \n\t"
    // load the immediate 0 into i (note, this does _not_ touch any flags)
    "ldi %0, 0x00       \n\t"
    // walk and chew gum at the same time
    "adc %0, r1          \n\t"
#else
    /* Multiply 8-bit i * 8-bit scale, giving 16-bit r1,r0 */
    "mul %0, %1    \n\t"
    /* Move the high 8-bits of the product (r1) back to i */
    "mov %0, r1    \n\t"
#endif
    /* R1 IS LEFT DIRTY HERE; YOU MUST ZERO IT OUT YOURSELF  */
    /* "clr __zero_reg__    \n\t" */

    : "+a" (i)      /* writes to i */
    : "a"  (scale)  /* uses scale */
    : "r0", "r1"    /* clobbers r0, r1 */ );

  // Return the result
  return i;
}

uint8_t scale8_video_LEAVING_R1_DIRTY( uint8_t i, fract8 scale) {
  uint8_t j = 0;
  asm volatile(
    "  tst %[i]\n\t"
    "  breq L_%=\n\t"
    "  mul %[i], %[scale]\n\t"
    "  mov %[j], r1\n\t"
    "  breq L_%=\n\t"
    "  subi %[j], 0xFF\n\t"
    "L_%=: \n\t"
    : [j] "+a" (j)
    : [i] "a" (i), [scale] "a" (scale)
    : "r0", "r1");
  return j;
}

struct CRGB;
struct CHSV;
void hsv2rgb_rainbow( const CHSV& hsv, CRGB& rgb);
void hsv2rgb_spectrum( const CHSV& hsv, CRGB& rgb);

struct CHSV {
  union {
    struct {
      union {
        uint8_t hue;
        uint8_t h;
      };
      union {
        uint8_t saturation;
        uint8_t sat;
        uint8_t s;
      };
      union {
        uint8_t value;
        uint8_t val;
        uint8_t v;
      };
    };
    uint8_t raw[3];
  };

  CHSV( uint8_t ih, uint8_t is, uint8_t iv) : h(ih), s(is), v(iv) {}
};

struct CRGB {
  union {
    struct {
      union {
        uint8_t g;
        uint8_t green;
      };
      union {
        uint8_t r;
        uint8_t red;
      };
      union {
        uint8_t b;
        uint8_t blue;
      };
    };
    uint8_t raw[3];
  };
  CRGB() {}
  CRGB( uint8_t ir, uint8_t ig, uint8_t ib) : g(ig), r(ir), b(ib) {}
  CRGB& operator= (const uint32_t colorcode) {
    r = (colorcode >> 16) & 0xFF;
    g = (colorcode >>  8) & 0xFF;
    b = (colorcode >>  0) & 0xFF;
    return *this;
  }
  CRGB& operator= (const CHSV& rhs) {
    hsv2rgb_spectrum(rhs, *this);
    return *this;
  }
  //  CRGB& operator/= (uint8_t d ) {
  //    r /= d;
  //    g /= d;
  //    b /= d;
  //    return *this;
  //  }
  CRGB& nscale8 (uint8_t scale ) {
    r = ((int)r * (int)(scale) ) >> 8;
    g = ((int)g * (int)(scale) ) >> 8;
    b = ((int)b * (int)(scale) ) >> 8;
    //    nscale8x3(r, g, b, scaledown);
    return *this;
  }
};

CRGB operator+( const CRGB& p1, const CRGB& p2) {
  return CRGB( qadd8( p1.r, p2.r),
               qadd8( p1.g, p2.g),
               qadd8( p1.b, p2.b));
}

CRGB operator/( const CRGB& p1, uint8_t d) {
  return CRGB( p1.r / d, p1.g / d, p1.b / d);
}


#define APPLY_DIMMING(X) (X)
#define HSV_SECTION_6 (0x20)
#define HSV_SECTION_3 (0x40)

void hsv2rgb_raw_avr(const struct CHSV & hsv, struct CRGB & rgb){
  uint8_t hue, saturation, value;

  hue =        hsv.hue;
  saturation = hsv.sat;
  value =      hsv.val;

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
  uint8_t offset = hsv.hue & (HSV_SECTION_3 - 1);  // 0..63
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
    rgb.r = rampup_adj_with_floor;
    rgb.g = brightness_floor;
    rgb.b = rampdown_adj_with_floor;
  } else {
    if ( hue & 0x40) {
      // section 1: 0x40..0x7F
      rgb.r = brightness_floor;
      rgb.g = rampdown_adj_with_floor;
      rgb.b = rampup_adj_with_floor;
    } else {
      // section 0: 0x00..0x3F
      rgb.r = rampdown_adj_with_floor;
      rgb.g = rampup_adj_with_floor;
      rgb.b = brightness_floor;
    }
  }

  //    cleanup_R1();
  asm volatile( "clr __zero_reg__  \n\t" : : : "r1" );
}
// End of AVR asm implementation


void hsv2rgb_spectrum( const CHSV& hsv, CRGB& rgb)
{
  CHSV hsv2(hsv);
  hsv2.hue = scale8( hsv2.hue, 191);
  hsv2rgb_raw_avr(hsv2, rgb);
}


// Sometimes the compiler will do clever things to reduce
// code size that result in a net slowdown, if it thinks that
// a variable is not used in a certain location.
// This macro does its best to convince the compiler that
// the variable is used in this location, to help control
// code motion and de-duplication that would result in a slowdown.
#define FORCE_REFERENCE(var)  asm volatile( "" : : "r" (var) )


#define K255 255
#define K171 171
#define K170 170
#define K85  85

void hsv2rgb_rainbow( const CHSV& hsv, CRGB& rgb)
{
  // Yellow has a higher inherent brightness than
  // any other color; 'pure' yellow is perceived to
  // be 93% as bright as white.  In order to make
  // yellow appear the correct relative brightness,
  // it has to be rendered brighter than all other
  // colors.
  // Level Y1 is a moderate boost, the default.
  // Level Y2 is a strong boost.
  const uint8_t Y1 = 1;
  const uint8_t Y2 = 0;

  // G2: Whether to divide all greens by two.
  // Depends GREATLY on your particular LEDs
  const uint8_t G2 = 0;

  // Gscale: what to scale green down by.
  // Depends GREATLY on your particular LEDs
  const uint8_t Gscale = 0;


  uint8_t hue = hsv.hue;
  uint8_t sat = hsv.sat;
  uint8_t val = hsv.val;

  uint8_t offset = hue & 0x1F; // 0..31

  // offset8 = offset * 8
  uint8_t offset8 = offset;
  {
#if defined(__AVR__)
    // Left to its own devices, gcc turns "x <<= 3" into a loop
    // It's much faster and smaller to just do three single-bit shifts
    // So this business is to force that.
    offset8 <<= 1;
    asm volatile("");
    offset8 <<= 1;
    asm volatile("");
    offset8 <<= 1;
#else
    // On ARM and other non-AVR platforms, we just shift 3.
    offset8 <<= 3;
#endif
  }

  uint8_t third = scale8( offset8, (256 / 3)); // max = 85

  uint8_t r, g, b;

  if ( ! (hue & 0x80) ) {
    // 0XX
    if ( ! (hue & 0x40) ) {
      // 00X
      //section 0-1
      if ( ! (hue & 0x20) ) {
        // 000
        //case 0: // R -> O
        r = K255 - third;
        g = third;
        b = 0;
        FORCE_REFERENCE(b);
      } else {
        // 001
        //case 1: // O -> Y
        if ( Y1 ) {
          r = K171;
          g = K85 + third ;
          b = 0;
          FORCE_REFERENCE(b);
        }
        if ( Y2 ) {
          r = K170 + third;
          //uint8_t twothirds = (third << 1);
          uint8_t twothirds = scale8( offset8, ((256 * 2) / 3)); // max=170
          g = K85 + twothirds;
          b = 0;
          FORCE_REFERENCE(b);
        }
      }
    } else {
      //01X
      // section 2-3
      if ( !  (hue & 0x20) ) {
        // 010
        //case 2: // Y -> G
        if ( Y1 ) {
          //uint8_t twothirds = (third << 1);
          uint8_t twothirds = scale8( offset8, ((256 * 2) / 3)); // max=170
          r = K171 - twothirds;
          g = K170 + third;
          b = 0;
          FORCE_REFERENCE(b);
        }
        if ( Y2 ) {
          r = K255 - offset8;
          g = K255;
          b = 0;
          FORCE_REFERENCE(b);
        }
      } else {
        // 011
        // case 3: // G -> A
        r = 0;
        FORCE_REFERENCE(r);
        g = K255 - third;
        b = third;
      }
    }
  } else {
    // section 4-7
    // 1XX
    if ( ! (hue & 0x40) ) {
      // 10X
      if ( ! ( hue & 0x20) ) {
        // 100
        //case 4: // A -> B
        r = 0;
        FORCE_REFERENCE(r);
        //uint8_t twothirds = (third << 1);
        uint8_t twothirds = scale8( offset8, ((256 * 2) / 3)); // max=170
        g = K171 - twothirds; //K170?
        b = K85  + twothirds;

      } else {
        // 101
        //case 5: // B -> P
        r = third;
        g = 0;
        FORCE_REFERENCE(g);
        b = K255 - third;

      }
    } else {
      if ( !  (hue & 0x20)  ) {
        // 110
        //case 6: // P -- K
        r = K85 + third;
        g = 0;
        FORCE_REFERENCE(g);
        b = K171 - third;

      } else {
        // 111
        //case 7: // K -> R
        r = K170 + third;
        g = 0;
        FORCE_REFERENCE(g);
        b = K85 - third;

      }
    }
  }

  // This is one of the good places to scale the green down,
  // although the client can scale green down as well.
  if ( G2 ) g = g >> 1;
  if ( Gscale ) g = scale8_video_LEAVING_R1_DIRTY( g, Gscale);

  // Scale down colors if we're desaturated at all
  // and add the brightness_floor to r, g, and b.
  if ( sat != 255 ) {
    if ( sat == 0) {
      r = 255; b = 255; g = 255;
    } else {
      //nscale8x3_video( r, g, b, sat);
#if (FASTLED_SCALE8_FIXED==1)
      if ( r ) r = scale8_LEAVING_R1_DIRTY( r, sat);
      if ( g ) g = scale8_LEAVING_R1_DIRTY( g, sat);
      if ( b ) b = scale8_LEAVING_R1_DIRTY( b, sat);
#else
      if ( r ) r = scale8_LEAVING_R1_DIRTY( r, sat) + 1;
      if ( g ) g = scale8_LEAVING_R1_DIRTY( g, sat) + 1;
      if ( b ) b = scale8_LEAVING_R1_DIRTY( b, sat) + 1;
#endif
      //cleanup_R1();
      asm volatile( "clr __zero_reg__  \n\t" : : : "r1" );

      uint8_t desat = 255 - sat;
      desat = scale8( desat, desat);

      uint8_t brightness_floor = desat;
      r += brightness_floor;
      g += brightness_floor;
      b += brightness_floor;
    }
  }

  // Now scale everything down if we're at value < 255.
  if ( val != 255 ) {

    val = scale8_video_LEAVING_R1_DIRTY( val, val);
    if ( val == 0 ) {
      r = 0; g = 0; b = 0;
    } else {
      // nscale8x3_video( r, g, b, val);
#if (FASTLED_SCALE8_FIXED==1)
      if ( r ) r = scale8_LEAVING_R1_DIRTY( r, val);
      if ( g ) g = scale8_LEAVING_R1_DIRTY( g, val);
      if ( b ) b = scale8_LEAVING_R1_DIRTY( b, val);
#else
      if ( r ) r = scale8_LEAVING_R1_DIRTY( r, val) + 1;
      if ( g ) g = scale8_LEAVING_R1_DIRTY( g, val) + 1;
      if ( b ) b = scale8_LEAVING_R1_DIRTY( b, val) + 1;
#endif
      //cleanup_R1();
      asm volatile( "clr __zero_reg__  \n\t" : : : "r1" );
    }
  }

  // Here we have the old AVR "missing std X+n" problem again
  // It turns out that fixing it winds up costing more than
  // not fixing it.
  // To paraphrase Dr Bronner, profile! profile! profile!
  //asm volatile(  ""  :  :  : "r26", "r27" );
  //asm volatile (" movw r30, r26 \n" : : : "r30", "r31");
  rgb.r = r;
  rgb.g = g;
  rgb.b = b;
}


#define RAND16_SEED  1337
#define FASTLED_RAND16_2053  ((uint16_t)(2053))
#define FASTLED_RAND16_13849 ((uint16_t)(13849))
uint16_t rand16seed = RAND16_SEED;

void random16_set_seed( uint16_t seed) {
  rand16seed = seed;
}

uint8_t random8() {
  rand16seed = (rand16seed * FASTLED_RAND16_2053) + FASTLED_RAND16_13849;
  return (uint8_t)(((uint8_t)(rand16seed & 0xFF)) + ((uint8_t)(rand16seed >> 8)));
}

uint8_t random8(uint8_t lim) {
  uint8_t r = random8();
  r = (r * lim) >> 8;
  return r;
}

uint8_t random8(uint8_t min, uint8_t lim) {
  uint8_t delta = lim - min;
  uint8_t r = random8(delta) + min;
  return r;
}

uint16_t random16() {
  rand16seed = (rand16seed * FASTLED_RAND16_2053) + FASTLED_RAND16_13849;
  return rand16seed;
}

uint16_t random16( uint16_t lim) {
  uint16_t r = random16();
  uint32_t p = (uint32_t)lim * (uint32_t)r;
  r = p >> 16;
  return r;
}

uint16_t random16( uint16_t min, uint16_t lim) {
  uint16_t delta = lim - min;
  uint16_t r = random16(delta) + min;
  return r;
}

uint8_t triwave8(uint8_t in) {
  if ( in & 0x80) {
    in = 255 - in;
  }
  uint8_t out = in << 1;
  return out;
}

void * memset8 ( void * ptr, uint8_t val, uint16_t num )
{
  asm volatile(
    "  movw r26, %[ptr]        \n\t"
    "  sbrs %A[num], 0         \n\t"
    "  rjmp Lseteven_%=        \n\t"
    "  rjmp Lsetodd_%=         \n\t"
    "Lsetloop_%=:              \n\t"
    "  st X+, %[val]           \n\t"
    "Lsetodd_%=:               \n\t"
    "  st X+, %[val]           \n\t"
    "Lseteven_%=:              \n\t"
    "  subi %A[num], 2         \n\t"
    "  brcc Lsetloop_%=        \n\t"
    "  sbci %B[num], 0         \n\t"
    "  brcc Lsetloop_%=        \n\t"
    : [num] "+r" (num)
    : [ptr]  "r" (ptr),
    [val]  "r" (val)
    : "memory"
  );
  return ptr;
}

#endif

uint8_t qadd8( uint8_t a, uint8_t b, uint8_t c) {
  unsigned int t = a + b + c;
  if ( t > 255) t = 255;
  return t;
}

#endif
