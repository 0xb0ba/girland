#ifndef __INC_MINILED_TOOLS_H
#define __INC_MINILED_TOOLS_H

//math
uint8_t qadd8( uint8_t a, uint8_t b) {
  unsigned int t = a + b;
  if ( t > 255) t = 255;
  return t;
}

uint8_t qadd8( uint8_t a, uint8_t b, uint8_t c) {
  unsigned int t = a + b + c;
  if ( t > 255) t = 255;
  return t;
}

uint8_t scale8( uint8_t i, uint8_t scale) {
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

uint8_t triwave8(uint8_t in) {
  if ( in & 0x80) {
    in = 255 - in;
  }
  uint8_t out = in << 1;
  return out;
}

//////rand
#define RAND16_SEED  1337
#define FASTLED_RAND16_2053  ((uint16_t)(2053))
#define FASTLED_RAND16_13849 ((uint16_t)(13849))
uint16_t rand16seed = RAND16_SEED;

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

void random16_set_seed( uint16_t seed) {
  rand16seed = seed;
}

///////memory
void * memset8 ( void * ptr, uint8_t val, uint16_t num ) {
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
    : [num] "+d" (num)
    : [ptr]  "r" (ptr),
    [val]  "r" (val)
    : "memory"
  );
  return ptr;
}

#define APPLY_DIMMING(X) (X)
#define HSV_SECTION_6 (0x20)
#define HSV_SECTION_3 (0x40)

#endif //#ifndef __INC_MINILED_TOOLS_H
