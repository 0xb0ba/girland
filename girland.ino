#include "FastLED.h"          // библиотека для работы с лентой

// число светодиодов в кольце/ленте
#define LED_COUNT 160
//стартовый светодиод звезды
#define LED_STAR_START 150
//длина звезды
#define LED_STAR_LENGHT 10
// пин, куда подключен DIN ленты
#define LED_DT 13
#define PHOTO_SENSOR 2

// максимальная яркость (0 - 255)
#define MAX_BRIGHT 255
// адаптивная подсветка
#define ADAPT_LIGHT

struct CRGB leds[LED_COUNT + 1];

unsigned long now;
unsigned long time2exit;

int check() {
    now = millis();

#ifdef ADAPT_LIGHT
    int new_bright = 0x01 | map(analogRead(PHOTO_SENSOR), 1, 1023, 5,
                                MAX_BRIGHT);   // считать показания с фоторезистора, перевести диапазон
    LEDS.setBrightness(new_bright);        // установить новую яркость
//    Serial.print("bright:");
//    Serial.println(new_bright);
#endif

    return now < time2exit;
}

void show(int thisdelay) {
#ifdef LED_STAR_START
    struct CRGB star[LED_STAR_LENGHT];
    memcpy(&star[0], &leds[LED_STAR_START], LED_STAR_LENGHT * sizeof(CRGB));
    for (int n = LED_STAR_START; n < LED_STAR_START + LED_STAR_LENGHT; n++) {
        leds[n].r = qadd8(qadd8(leds[n].r, leds[n].g), leds[n].b);
        leds[n].g /= 8;
        leds[n].b /= 8;
    }
#endif
    LEDS.show();
#ifdef LED_STAR_START
    memcpy(&leds[LED_STAR_START], &star[0], LED_STAR_LENGHT * sizeof(CRGB));
#endif
    delay(thisdelay);
}

void rnd(int x) {
    leds[x].r = random8(0, 255);
    leds[x].g = random8(0, 255);
    leds[x].b = random8(0, 255);
}

void ror() {
    memmove(&leds[1], &leds[0], LED_COUNT * sizeof(CRGB));
    memcpy(&leds[0], &leds[LED_COUNT], sizeof(CRGB));
}

void rol() {
    memcpy(&leds[LED_COUNT], &leds[0], sizeof(CRGB));
    memmove(&leds[0], &leds[1], LED_COUNT * sizeof(CRGB));
}

// плавная смена цветов всей ленты
void rainbow_fade(int thisdelay, int thishue) {                         //-m2-FADE ALL LEDS THROUGH HSV RAINBOW
    uint8_t h=random8(0,3);
    uint8_t s=random8(0,3);
    uint8_t v=random8(0,3);
    uint8_t sat;
    uint8_t val;
    while (check()) {
        for (int i = 0; i < LED_COUNT; i++)
            leds[i] = CHSV((uint8_t)thishue, triwave8(sat), triwave8(val));
        show(thisdelay);
        thishue=thishue+h;
        sat+=s;
        val+=v;
    }
}

// крутящаяся радуга
// крутая плавная вращающаяся радуга
void rainbow_loop(int thisdelay, int mode) {                        //-m3-LOOP HSV RAINBOW
    int thisdelay2 = random8(0, 100);
    int cnt = random8(1, 10);
    int inc = random8(1, 10);
    int ihue;
    while (check()) {
        for (unsigned int i = 0; i < LED_COUNT; i++) {
            leds[i] = CHSV((i * 255 / LED_COUNT * cnt + ihue) & 0xff, 255, 255);
            if (mode) show(thisdelay);
        }
        show(thisdelay2);
        ihue += inc;
    }
}

// случайная смена цветов
void random_burst(int thisdelay) {                         //-m4-RANDOM INDEX/COLOR
    while (check()) {
        rnd(random8(0, LED_COUNT));
        show(thisdelay);
    }
}

// бегающий паровозик светодиодов
void color_bounceFADE(int thisdelay, int thishue) {
    memset8(leds, 0, sizeof(leds));
    int cnt = random8(1, 10);
    for (int i = 0; i < cnt; i++)
        leds[i] = CHSV(thishue, 255, triwave8(i*255/cnt));
    int dir = 1;
    while (check()) {
        for (int i = 0; i < LED_COUNT - cnt - 1; i++) {
            if (dir) ror(); else rol();
            show(thisdelay);
        }
        dir = !dir;
    }
}

// вращается несколько цветов сплошные или одиночные
// вращается половина красных и половина синих
void ems_lightsALL(int thisdelay, int8_t mode, int8_t dir) {         //-m7-EMERGENCY LIGHTS (TWO COLOR SINGLE LED)
    int8_t cnt = random8(1, LED_COUNT / 10 + 1);
    for (int8_t i = 0; i < cnt; i++) {
        for (int n = 0; n < LED_COUNT / cnt; n++) {
            if (mode || !n) rnd(0);
            if (dir) ror(); else rol();
            show(thisdelay);
        }
    }
    while (check()) {
        if (dir) ror(); else rol();
        show(thisdelay);
    }
}

// случайный стробоскоп
void flicker() {                          //-m9-FLICKER EFFECT
    int m = random8(1, 255 / 2);
    while (check()) {
        int thishue = random8(0, 255);
        int random_bright = random8(0, 255);
        int random_delay = random8(10, 100);
        int random_bool = random8(0, random_bright);
        if (random_bool < m) {
            for (int i = 0; i < LED_COUNT; i++)
                leds[i] = CHSV(thishue, 255, random_bright);
            show(random_delay);
        }
    }
}

//// пульсация со сменой цветов
//void pulse_one_color_all_rev(int thisdelay, int thishue) {              //-m10-PULSE BRIGHTNESS ON ALL LEDS TO ONE COLOR
//    int inc = random8(1, 10);
//    int mode = random8(1, 4);
//    int ibright;
//    while (check()) {
//        ibright = 0;
//        while (ibright >= 0) {
//            for (int idex = 0; idex < LED_COUNT; idex++)
//                switch (mode) {
//                    case 1:
//                        leds[idex] = CHSV(thishue, 255, ibright);
//                        break;
//                    case 2:
//                        leds[idex] = CHSV(thishue, ibright, 255);
//                        break;
//                    case 3:
//                        leds[idex] = CHSV(ibright, 255, 255);
//                        break;
//                }
//            show(thisdelay);
//            ibright += inc;
//            if (ibright > 255) {
//                inc = -inc;
//                ibright = 255;
//            }
//        }
//        inc = -inc;
//    }
//}

// плавная смена яркости по вертикали (для кольца)
void fade_vertical(int thisdelay, int thishue) {                    //-m12-FADE 'UP' THE LOOP
    uint8_t inc = random8(2, 128);
    uint8_t ibright;
    while (check()) {
        for (int i = 0; i < LED_COUNT / 2; i++) {
            CHSV chsv(thishue, 255, triwave8(ibright));
            leds[i] = chsv;
            leds[LED_COUNT - 1 - i] = chsv;
            show(thisdelay);
            ibright += inc;
        }
    }
}

// случайная смена цветов
// безумие красных светодиодов
void random_red(int thisdelay, int thishue, int mode) {         //QUICK 'N DIRTY RANDOMIZE TO GET CELL AUTOMATA STARTED
    int thissat = random8(0, 255);
    int dutty = random8(0, 100);
    while (check()) {
        int x = random8(0, LED_COUNT);
        if (random8(0, 100) < dutty) {
            if (mode) rnd(x);
            else leds[x] = CHSV(thishue, thissat, random8(0, 255));
        } else {
            leds[x] = 0;
        }
        show(thisdelay);
    }
}

// вращаются случайные цвета (паравозики по одному диоду)
// безумие случайных цветов
void random_march(int thisdelay, int mode) {                   //-m14-RANDOM MARCH CCW
    while (check()) {
        if (mode) ror(); else rol();
        rnd(0);
        show(thisdelay);
    }
}

// пульсирует значок радиации
void radiation(int thisdelay, int thishue, int mode) {                   //-m16-SORT OF RADIATION SYMBOLISH-
    int even = random8(0, 2);
    int cnt = random8(1, 10);
    uint8_t ibright;
    while (check()) {
        for (int i = 0; i < LED_COUNT; i++) {
            if ((i*cnt/LED_COUNT) % 2 == even)
                leds[i] = CHSV(thishue, 255, triwave8(ibright));
        }
        show(thisdelay);
        if (!++ibright) {
            if (mode) thishue = random8(0, 255);
        }
    }
}

// красный светодиод бегает по кругу
void color_loop_vardelay(int thishue, int mode, int dir) {       //-m17-COLOR LOOP (SINGLE LED) w/ VARIABLE DELAY
    int x = random8(0, LED_COUNT);
    while (check()) {
        if (mode) thishue = random8(0, 255);
        leds[0] = CHSV(thishue, 255, 255);
        for (int i = 0; i < LED_COUNT; i++) {
            if (dir) ror(); else rol();
            int di = abs(x - i);
            int t = constrain((10 / di) * 10, 10, 500);
            show(t);
        }
    }
}

//переливы
void sin_bright_wave(int thisdelay, int thishue, int mode) {        //-m19-BRIGHTNESS SINE WAVE
    uint8_t inc = random8(2, 128);
    uint8_t ibright;
    while (check()) {
        for (int i = 0; i < LED_COUNT; i++) {
            uint8_t b = triwave8(ibright);
            leds[i] = CHSV(thishue, 255, b);
            show(thisdelay);
            ibright += inc;
            if (!b && mode) thishue = random8(0, 255);
        }
    }
}

// вспышки спускаются в центр
// красные вспышки спускаются вниз
void pop_horizontal(int thisdelay, int thishue, int mode) {        //-m20-POP FROM LEFT TO RIGHT UP THE RING
    int thishue2 = thishue;
    while (check()) {
        for (int i = 0; i < LED_COUNT / 2; i++) {
            memset8(leds, 0, sizeof(leds));
            leds[i] = CHSV(thishue, 255, 255);
            leds[LED_COUNT - 1 - i] = CHSV(thishue2, 255, 255);
            show(thisdelay);
        }
        if (mode) {
            thishue = random8(0, 255);
            thishue2 = random8(0, 255);
        }
    }
}

// эффект пламени
void flame() {                                    //-m22-FLAMEISH EFFECT
    while (check()) {
        int idelay = random8(0, 35);
        float hinc = (90 / (float) LED_COUNT) + random8(0, 3);
        float ihue = 0;
        for (int i = 0; i <= LED_COUNT / 2; i++) {
            ihue = ihue + hinc;
            CHSV chsv(ihue, 255, 255);
            leds[i] = chsv;
            leds[LED_COUNT - 1 - i] = chsv;
            show(idelay);
        }
    }
}

// радуга в вертикаьной плоскости (кольцо)
void rainbow_vertical(int thisdelay) {                        //-m23-RAINBOW 'UP' THE LOOP
    int inc = random8(0, 30);
    int ihue;
    while (check()) {
        for (int i = 0; i <= LED_COUNT / 2; i++) {
            ihue = (ihue + inc) & 0xff;
            CHSV chsv(ihue, 255, 255);
            leds[i] = chsv;
            leds[LED_COUNT - 1 - i] = chsv;
            show(thisdelay);
        }
    }
}

// полицейская мигалка
void ems_lightsSTROBE() {                  //-m26-EMERGENCY LIGHTS (STROBE LEFT/RIGHT)
    while (check()) {
        for (int x = 0; x < 5; x++) {
            for (int i = 0; i < LED_COUNT / 2; i++) {
                leds[i].r = 0;
//              leds[i].g=0;
                leds[i].b = 255;
            }
            show(25);
            memset8(leds, 0, sizeof(leds));
            show(25);
        }
        for (int x = 0; x < 5; x++) {
            for (int i = LED_COUNT / 2; i < LED_COUNT; i++) {
                leds[i].r = 255;
//              leds[i].g=0;
                leds[i].b = 0;
            }
            show(25);
            memset8(leds, 0, sizeof(leds));
            show(25);
        }
    }
}

// уровень звука
// случайные вспышки красного в вертикаьной плоскости
void kitt(int thishue) {                                      //-m28-KNIGHT INDUSTIES 2000
    while (check()) {
        int rand = random8(1, LED_COUNT / 2);
        for (int i = 0; i < rand; i++) {
            CHSV chsv(thishue, 255, 255);
            leds[LED_COUNT / 2 + i] = chsv;
            leds[LED_COUNT / 2 - i] = chsv;
            show(100 / rand);
        }
        int rand2 = random8(1, LED_COUNT / 2);
        for (int i = rand; i > 0; i--) {
            leds[LED_COUNT / 2 + i] = 0;
            leds[LED_COUNT / 2 - i] = 0;
            show(100 / rand2);
        }
    }
}

// зелёненькие бегают по кругу случайно
void matrix(int thisdelay, int thishue, int mode, int dir) {                                   //-m29-ONE LINE MATRIX
    while (check()) {
        int rand = random8(0, 100);
        if (rand > 90)
            leds[0] = CHSV(thishue, 255, 255);
        else {
            leds[0] = 0;
        }
        if (dir) ror(); else rol();
        show(thisdelay);
        if (mode)
            thishue = random8(0, 255);
    }
}

//// там rainbow_loop инкремент подвинуть и тоже самое
//// крутая плавная вращающаяся радуга
//void new_rainbow_loop(int thisdelay) {                      //-m88-RAINBOW FADE FROM FAST_SPI2
//    int thishue;
//    while (check()) {
//        fill_rainbow(leds, LED_COUNT, thishue);
//        show();
//        delay(thisdelay);
//        thishue++;
//    }
//}

//-----------------------------плавное заполнение цветом-----------------------------------------
void colorWipe(int thisdelay, int thishue, int mode, int dir) {
    while (check()) {
        for (int i = 0; i < LED_COUNT * 2; i++) {
            if (i < LED_COUNT) leds[0] = CHSV(thishue, 255, 255);
            else {
                leds[0] = 0;
            }
            if (dir) ror(); else rol();
            show(thisdelay);
        }
        if (mode)thishue = random8(0, 255);
    }
}

void setup() {
    Serial.begin(9600);              // открыть порт для связи
    random16_set_seed(analogRead(0));
    LEDS.addLeds<WS2811, LED_DT, GRB>(leds, LED_COUNT);  // настрйоки для нашей ленты (ленты на WS2811, WS2812, WS2812B)
    LEDS.setBrightness(MAX_BRIGHT);        // установить новую яркость
}

void loop() {
    check();
    time2exit = random16(5000, 30000);
    Serial.print("duration:");
    Serial.println(time2exit);
    time2exit += now;
    uint8_t effect = random8(0, 21);
    Serial.print("effect:");
    Serial.println(effect);
    uint8_t thisdelay = random8(0, 100);
    Serial.print("delay:");
    Serial.println(thisdelay);
    uint8_t thishue = random8(0, 255);
    Serial.print("hue:");
    Serial.println(thishue);
    uint8_t mode = random8(0, 2);
    Serial.print("mode:");
    Serial.println(mode);
    uint8_t dir = random8(0, 2);
    Serial.print("dir:");
    Serial.println(dir);
    switch (effect) {
        case 0:
            rainbow_fade(thisdelay, thishue);
            break;
        case 1:
            rainbow_loop(thisdelay, mode);
            break;
        case 2:
            random_burst(thisdelay);
            break;
        case 3:
            color_bounceFADE(thisdelay, thishue);
            break;
        case 4:
            random_burst(thisdelay);
            break;
        case 5:
            ems_lightsALL(thisdelay, mode, dir);
            break;
        case 6:
            flicker();
            break;
        case 7:
            fade_vertical(thisdelay, thishue);
            break;
        case 8:
            random_red(thisdelay, thishue, mode);
            break;
        case 9:
            random_march(thisdelay, mode);
            break;
        case 10:
            radiation(thisdelay, thishue, mode);
            break;
        case 11:
            color_loop_vardelay(thishue, mode, dir);
            break;
        case 12:
            sin_bright_wave(thisdelay, thishue, mode);
            break;
        case 13:
            pop_horizontal(thisdelay, thishue, mode);
            break;
        case 14:
            flame();
            break;
        case 15:
            rainbow_vertical(thisdelay);
            break;
        case 16:
            ems_lightsSTROBE();
            break;
        case 17:
            kitt(thishue);
            break;
        case 18:
            matrix(thisdelay, thishue, mode, dir);
            break;
        case 20:
            colorWipe(thisdelay, thishue, mode, dir);
            break;
    }
}

/*
кетч использует 9662 байт (31%) памяти устройства. Всего доступно 30720 байт.
Глобальные переменные используют 813 байт (39%) динамической памяти, оставляя 1235 байт для локальных переменных. Максимум: 2048 байт.

Скетч использует 9408 байт (30%) памяти устройства. Всего доступно 30720 байт.
Глобальные переменные используют 813 байт (39%) динамической памяти, оставляя 1235 байт для локальных переменных. Максимум: 2048 байт.

Скетч использует 9370 байт (30%) памяти устройства. Всего доступно 30720 байт.
Глобальные переменные используют 813 байт (39%) динамической памяти, оставляя 1235 байт для локальных переменных. Максимум: 2048 байт.

Скетч использует 9414 байт (30%) памяти устройства. Всего доступно 30720 байт.
Глобальные переменные используют 813 байт (39%) динамической памяти, оставляя 1235 байт для локальных переменных. Максимум: 2048 байт.

Скетч использует 9054 байт (29%) памяти устройства. Всего доступно 30720 байт.
Глобальные переменные используют 809 байт (39%) динамической памяти, оставляя 1239 байт для локальных переменных. Максимум: 2048 байт.
*/
