// число светодиодов в кольце/ленте
#define LED_COUNT 160
// общее число доступных эффектов вычисляется по спискам EFFECT_LIST_* ниже
//стартовый светодиод звезды
#define LED_STAR_START 150
//длина звезды
#define LED_STAR_LENGTH 10
#ifdef LED_STAR_START
static_assert(LED_STAR_START + LED_STAR_LENGTH <= LED_COUNT,
              "LED_STAR_START + LED_STAR_LENGTH exceeds LED_COUNT!");
#endif
//пин, куда подключен DIN ленты
#define LED_DT 13

// =============================================================================
// ГЕОМЕТРИЯ ОКНА / АРКИ (для симметричных эффектов)
// =============================================================================
// SYM_SEG_CNT: количество сторон симметрии (2 - лево/право, 3 - П-арка, 4 - контур рамы)
#define SYM_SEG_CNT 2
// SYM_SEG_DIR: чередование направления обхода сторон (1 - каждый второй зеркально, 2 - попарно)
#define SYM_SEG_DIR 1
// Длина одной симметричной стороны (константа времени компиляции)
#define SYM_SEG_LEN ((int16_t)(LED_COUNT / SYM_SEG_CNT))

// =============================================================================
// НАСТРОЙКА ФОТОСЕНСОРА И АДАПТИВНОЙ ЯРКОСТИ
// =============================================================================
// Подключение к соседним аналоговым пинам A1, A2, A3 (чтобы не тянуть провода к
// питанию):
#define PHOTO_GND_PIN A1 // виртуальная земля GND (LOW) для датчика
#define PHOTO_SENSOR A2 // аналоговый вход сигнала фотосенсора
#define PHOTO_VCC_PIN A3 // виртуальное питание VCC +5V (HIGH) для датчика

// Режим 2-пинового фоторезистора (true: встроенная подтяжка к 5V, вставляется
// прямо в A1 и A2)
#define PHOTO_INTERNAL_PULLUP false

// Инверсия датчика (false: светлее = больше analogRead; true: светлее = меньше
// analogRead)
#define PHOTO_INVERT false

// Диапазон автоматической регулировки яркости (от полной темноты до яркого
// света):
#define MIN_BRIGHT 5 // минимальная яркость в темноте (уютный ночник, 1..255)
#define MAX_BRIGHT 100 // максимальная яркость при дневном свете (1..255)

// Включение адаптивной подсветки по фотосенсору
#define ADAPT_LIGHT

#define COLOR_DEBTH 3
#define ORDER_GRB
#include "miniled.h"
#include <avr/wdt.h>
#include <util/delay_basic.h>

static void fast_delay_ms(uint8_t ms) {
  while (ms--) {
    _delay_loop_2(4000); // 1 ms при 16 МГц (4000 * 4 такта = 16000 тактов)
    wdt_reset();
  }
}

// Отключение Watchdog сразу при старте МК (до инициализации .data и .bss)
// Защищает от циклического перезапуска при WDT на любых версиях загрузчика Nano
void wdt_init(void) __attribute__((naked)) __attribute__((section(".init3")));
void wdt_init(void) {
  MCUSR = 0;
  wdt_disable();
}

// VideoRAM :)
TLED leds[LED_COUNT];
#ifdef LED_STAR_START
TLED star[LED_STAR_LENGTH];
#endif

//текущее время
unsigned long NOW;
//время выхода из эффекта
unsigned long TIME2EXIT;

//колво сегментов отображения
uint8_t SEG_CNT;
//колво сегментов одного направления
uint8_t SEG_DIR;
//длина сегмента отображения
int16_t SEG_LEN;
// Параметры текущего эффекта (глобальный контекст)
uint8_t thisdelay;
uint8_t thishue;
uint8_t opt;
uint8_t mmode;
uint8_t dir;

// Битовые маски опций эффектов (переменная opt).
// ВАЖНО: это общие «соглашения», а не жёсткий контракт. Каждый эффект сам
// решает, что означают для него биты opt, и может трактовать их иначе.
// Глобальные mmode/dir используются только shift_stream и colorWipe.
#define OPT_MODE 1 // Бит 0: выбор подрежима работы (0 или 1)
#define OPT_DIR                                                                \
  2 // Бит 1: направление анимации (0 - назад/влево, 2 - вперед/вправо)
#define OPT_EVEN 4 // Бит 2: четность / альтернативная схема / плотность
#define OPT_HUE 8 // Бит 3: динамическое вращение цвета / радужный режим
#define OPT_SAT 16 // Бит 4: модуляция насыщенности цвета
#define OPT_VAL 32 // Бит 5: попиксельная или блочная модуляция яркости

TStrip LEDS = TStrip(leds, LED_COUNT, LED_DT);

uint8_t check() {
  wdt_reset();
  NOW = millis();
#ifdef ADAPT_LIGHT
  static unsigned long last_photo_check = 0;
  static uint16_t photo_min = 1023;
  static uint16_t photo_max = 0;

  if (NOW - last_photo_check >= 150) {
    last_photo_check = NOW;
    uint16_t raw = analogRead(PHOTO_SENSOR);

    // Запоминаем минимум и максимум измерений
    if (raw < photo_min)
      photo_min = raw;
    if (raw > photo_max)
      photo_max = raw;

    // Прямая линейная зависимость освещенности к яркости ленты
    uint8_t new_bright = MIN_BRIGHT;
    if (photo_max > photo_min) {
      new_bright += (uint16_t)(((uint32_t)(raw - photo_min) *
                                (uint8_t)(MAX_BRIGHT - MIN_BRIGHT)) /
                               (photo_max - photo_min));
    }
    LEDS.setBrightness(new_bright);
  }
#endif

  return (long)(TIME2EXIT - NOW) > 0;
}

void show() {
#ifdef LED_STAR_START
  memcpy(&star[0], &leds[LED_STAR_START], LED_STAR_LENGTH * sizeof(TLED));
  for (uint16_t n = LED_STAR_START; n < LED_STAR_START + LED_STAR_LENGTH; n++) {
    leds[n].set_r(qadd8(leds[n].get_r(), leds[n].get_g(), leds[n].get_b()));
    leds[n].set_g(leds[n].get_g() >> 3);
    leds[n].set_b(leds[n].get_b() >> 3);
  }
#endif
  LEDS.show();
#ifdef LED_STAR_START
  memcpy(&leds[LED_STAR_START], &star[0], LED_STAR_LENGTH * sizeof(TLED));
#endif
}

void swap(TLED *a, TLED *b, uint16_t l) {
  for (uint16_t n = 0; n < l; n++)
    a[l - n - 1] = b[n];
}

void show_delay(uint8_t delay_ms) {
  wdt_reset();
  show();
  fast_delay_ms(delay_ms);
  wdt_reset();
}

void showseg_delay(uint8_t delay_ms) {
  wdt_reset();
  for (uint8_t n = 1; n < SEG_CNT; n++) {
    if ((n / SEG_DIR) & 1) {
      swap(&leds[SEG_LEN * n], &leds[0], SEG_LEN);
    } else {
      memcpy(&leds[SEG_LEN * n], &leds[0], SEG_LEN * sizeof(TLED));
    }
  }
  // Если LED_COUNT не делится на SEG_CNT, добиваем хвост началом следующего
  // сегмента: так не остаётся мусора от прошлого кадра и «звезда» не гаснет.
  uint16_t covered = (uint16_t)SEG_LEN * SEG_CNT;
  uint16_t tail = (uint16_t)LED_COUNT - covered;
  if (tail) {
    if (((SEG_CNT / SEG_DIR) & 1) && tail <= (uint16_t)SEG_LEN) {
      // Зеркальный сегмент: первые tail пикселей — это перевёрнутые последние
      // tail пикселей сегмента 0.
      swap(&leds[covered], &leds[(uint16_t)(SEG_LEN - tail)], tail);
    } else {
      memcpy(&leds[covered], &leds[0], tail * sizeof(TLED));
    }
  }
  show();
  fast_delay_ms(delay_ms);
  wdt_reset();
}

void ror(uint16_t l) {
  TLED t = leds[l - 1];
  for (uint16_t i = l - 1; i > 0; i--)
    leds[i] = leds[i - 1];
  leds[0] = t;
}

void rol(uint16_t l) {
  TLED t = leds[0];
  for (uint16_t i = 0; i < l - 1; i++)
    leds[i] = leds[i + 1];
  leds[l - 1] = t;
}

void fade(uint8_t f) {
  for (uint16_t i = 0; i < LED_COUNT; i++)
    leds[i].nscale8(f);
}

void fill(uint16_t start, uint16_t len, TLED color) {
  for (uint16_t i = start; i < start + len; i++) {
    leds[i] = color;
  }
}

static inline uint8_t abs_diff8(uint8_t a, uint8_t b) {
  return (a > b) ? (a - b) : (b - a);
}

uint8_t cmp(TLED c1, TLED c2, uint8_t d) {
  return (abs_diff8(c1.get_r(), c2.get_r()) <= d) &&
         (abs_diff8(c1.get_g(), c2.get_g()) <= d) &&
         (abs_diff8(c1.get_b(), c2.get_b()) <= d);
}

TLED rnd() { return TLED(random8(), random8(), random8()); }
// =============================================================================
// Эффект 0: rainbow_cycle (Радужный цикл и бегущая радуга)
// Описание: Вращение палитры радуги по всей ленте или попиксельный градиент
//           с динамической модуляцией насыщенности, реверсом и глиттером.
// Параметры:
//   del (uint8_t) - задержка кадра (скорость вращения радуги)
//   hue (uint8_t) - начальный оттенок HSV (0..255)
//   opt (uint8_t) - битовая маска режимов:
//                   бит 32 (OPT_VAL):  1 - попиксельный градиент, 0 - сплошная
//                   заливка ленты бит 1  (OPT_MODE): режим динамического
//                   смещения оттенка бит 2  (OPT_DIR):  направление вращения
//                   радуги (вперед / назад) бит 4  (OPT_EVEN): синусоидальная
//                   модуляция насыщенности цвета бит 8  (OPT_HUE):  добавление
//                   мерцающих белых искорок (глиттер)
// =============================================================================
void rainbow_cycle() {
  uint8_t h = (random8() >> 3) - 16;
  uint8_t s = (random8() >> 3) - 16;
  uint8_t v = (random8() >> 3) - 16;
  uint8_t sat = 255, val = 255;
  uint8_t ss = 255, vv = 255;
  uint8_t isPerPixel = opt & 32;
  while (check()) {
    if (!isPerPixel && (opt & 1))
      thishue += h;
    if (opt & 2) {
      sat += s;
      ss = (opt & 4) ? triwave8(sat) : sat;
    }
    if (opt & 8) {
      val += v;
      vv = (opt & 16) ? triwave8(val) : val;
    }
    for (int16_t i = 0; i < SEG_LEN; i++) {
      if (isPerPixel && (opt & 1))
        thishue += h;
      leds[i] = CHSV(thishue, ss, vv);
    }
    if ((opt & 64) && random8(10) < 3) {
      leds[random16(0, SEG_LEN)] = TLED(255, 255, 255);
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 1: dendritic_crystal (Рост дендритных кристаллов / DLA)
// Описание: Симуляция диффузно-ограниченной агрегации (DLA): рост ледяных
//           кристаллов от 3 затравок навстречу друг другу, яркая вспышка полной
//           кристаллизации при смыкании и последующее таяние льда.
// Параметры:
//   thisdelay (uint8_t) - задержка между шагами роста кристаллов
//   thishue   (uint8_t) - базовый оттенок кристалла (+140..160 лазурно-ледяной)
// =============================================================================
void dendritic_crystal() {
  int16_t seed1 = 0;
  int16_t seed2 = SEG_LEN >> 1;
  int16_t seed3 = SEG_LEN - 1;
  int16_t r1 = 0, r2 = 0, r3 = 0;
  uint8_t state = 0; // 0=growing, 1=flash, 2=melting
  uint8_t meltCount = 0;
  memset8(leds, 0, SEG_LEN * sizeof(TLED));
  while (check()) {
    if (state == 0) {
      if (random8(10) < 6)
        r1++;
      if (random8(10) < 6)
        r2++;
      if (random8(10) < 6)
        r3++;
      TLED ice = CHSV(thishue + 140 + random8(20), random8(60, 160),
                      random8(180, 255));
      if (seed1 + r1 < SEG_LEN)
        leds[seed1 + r1] = ice;
      if (seed2 - r2 >= 0)
        leds[seed2 - r2] = ice;
      if (seed2 + r2 < SEG_LEN)
        leds[seed2 + r2] = ice;
      if (seed3 - r3 >= 0)
        leds[seed3 - r3] = ice;
      if ((seed1 + r1 >= seed2 - r2) || (seed2 + r2 >= seed3 - r3)) {
        state = 1;
      }
    } else if (state == 1) {
      fill(0, SEG_LEN, TLED(255, 255, 255));
      showseg_delay(30);
      if (!check())
        return;
      state = 2;
      meltCount = 0;
    } else {
      fade(180);
      if (random8(10) < 3)
        leds[random16(0, SEG_LEN)] = TLED(0, 0, 0);
      if (++meltCount > 18) {
        r1 = r2 = r3 = 0;
        state = 0;
        thishue += 30;
      }
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 2: heat_diffusion (Тепловая диффузия Лапласа)
// Описание: Физическая диффузия тепла по одномерному уравнению теплопроводности
//           с непрерывным сглаживанием соседних ячеек и стохастическими
//           искрами.
// Параметры:
//   thisdelay (uint8_t) - скорость остывания и распространения тепла
//   thishue   (uint8_t) - базовый цвет горячих искр
//   opt       (uint8_t) - битовая маска опций:
//                         OPT_EVEN: 1 - цветные искры (thishue), 0 -
//                         раскаленные белые угли
// =============================================================================
void heat_diffusion() {
  while (check()) {
    if (random8(10) < 3)
      leds[random16(0, SEG_LEN)] = (opt & OPT_EVEN)
                                       ? CHSV(thishue + random8(32), 220, 255)
                                       : TLED(255, 255, 200);
    for (int16_t i = 1; i < SEG_LEN - 1; i++) {
      TLED avg = blend(leds[i - 1], leds[i + 1], 128);
      leds[i] = blend(leds[i], avg, 160);
      leds[i].nscale8(245);
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 58: cellular_fire (Клеточный автомат пламени)
// Описание: Стохастический клеточный автомат горячих углей: энергия случайным
//           образом перескакивает на соседние ячейки ленты с затуханием.
// Параметры:
//   thisdelay (uint8_t) - темп горения клеточного автомата
//   thishue   (uint8_t) - цветовая температура пламени
// =============================================================================
void cellular_fire() {
  while (check()) {
    fade(185);
    if (random8(10) < 4) {
      uint16_t seed = random16(0, SYM_SEG_LEN);
      leds[seed] = CHSV(thishue + random8(32), 240, 255);
    }
    for (int16_t i = 1; i < SYM_SEG_LEN - 1; i++) {
      if (leds[i].get_r() > 100 || leds[i].get_g() > 100 ||
          leds[i].get_b() > 100) {
        if (random8(10) == 0) {
          int16_t n = (random8() & 1) ? (i + 1) : (i - 1);
          leds[n] = leds[i];
        }
      }
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 3: quantum_collapse (Квантовый коллапс волновой функции)
// Описание: Суперпозиция гармонических волн вероятности квантовой частицы;
//           при акте наблюдения волновая функция мгновенно коллапсирует
//           в точечное собственное состояние с яркой экспоненциальной вспышкой.
// Параметры:
//   thisdelay (uint8_t) - скорость эволюции фазы волновой функции
//   thishue   (uint8_t) - оттенок вероятностного квантового поля
// =============================================================================
void quantum_collapse() {
  uint8_t phase = 0;
  uint8_t collapsed = 0;
  int16_t eigenPos = SEG_LEN >> 1;
  int16_t rippleRadius = 0;
  while (check()) {
    if (collapsed == 0) {
      phase += 3;
      for (int16_t i = 0; i < SEG_LEN; i++) {
        uint8_t prob = (triwave8((i * 12 + phase) & 0xFF) >> 1) +
                       (triwave8((i * 8 - phase * 2) & 0xFF) >> 1);
        leds[i] = CHSV(thishue + (prob >> 3), 200, prob >> 2);
      }
      if (random8(30) == 0) {
        collapsed = 1;
        eigenPos = random16(4, SEG_LEN - 4);
        rippleRadius = 0;
        fill(0, SEG_LEN, TLED(0, 0, 0));
        leds[eigenPos] = TLED(255, 255, 255);
      }
    } else {
      fade(170);
      rippleRadius += 2;
      uint8_t br = (rippleRadius < 25) ? (255 - rippleRadius * 10) : 0;
      TLED c = CHSV(thishue + 128, 240, br);
      if (eigenPos - rippleRadius >= 0)
        leds[eigenPos - rippleRadius] = c;
      if (eigenPos + rippleRadius < SEG_LEN)
        leds[eigenPos + rippleRadius] = c;
      if (rippleRadius > 25) {
        collapsed = 0;
        thishue += 40;
      }
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 4: plasma_ball (Плазменный шар Теслы)
// Описание: Высокочастотный светящийся разряд следует за блуждающей точкой
//           касания пальца с реалистичными хаотическими флуктуациями дуги.
// Параметры:
//   thisdelay (uint8_t) - скорость блуждания центра разряда
//   thishue   (uint8_t) - спектральный цвет плазменного разряда
// =============================================================================
void plasma_ball() {
  int16_t center = SEG_LEN >> 1;
  uint16_t target = random16(0, SEG_LEN);
  uint8_t branchTimer = 0;
  while (check()) {
    fade(165);
    leds[center] = TLED(255, 255, 255);
    if (++branchTimer > 15) {
      branchTimer = 0;
      target = random16(0, SEG_LEN);
    }
    int16_t p = center + (((int32_t)(target - center) * random8(1, 255)) >> 8);
    leds[constrain(p, 0, SEG_LEN - 1)] = CHSV(thishue + random8(32), 220, 255);
    leds[target] = CHSV(thishue + 96, 200, 180);
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 59: nuclear_fission (Спонтанное деление ядра)
// Описание: Нестабильное тяжелое ядро самопроизвольно делится с выбросом
//           высокоэнергетических дочерних квантов в противоположные стороны.
// Параметры:
//   thisdelay (uint8_t) - скорость разлета дочерних частиц
//   thishue   (uint8_t) - цвет вспышки ядерного распада
// =============================================================================
void nuclear_fission() {
  int16_t center = SYM_SEG_LEN >> 1;
  while (check()) {
    fade(165);
    leds[center] = CHSV(96, 255, 100);
    if (center > 0)
      leds[center - 1] = CHSV(96, 255, 50);
    if (center < SYM_SEG_LEN - 1)
      leds[center + 1] = CHSV(96, 255, 50);
    if (random8(10) == 0) {
      int16_t p = constrain(center + (int8_t)random8(9) - 4, 0, SYM_SEG_LEN - 1);
      leds[p] = TLED(255, 255, 255);
      if (random8(3) == 0) {
        int16_t ray = constrain(p + (int8_t)random8(11) - 5, 0, SYM_SEG_LEN - 1);
        leds[ray] = CHSV(thishue + 130, 255, 220);
      }
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 5: shift_stream (Сдвиговый регистр / Потоки данных и лавы)
// Описание: Высокоскоростной аппаратный сдвиг пикселей в кольцевом буфере:
//           пакеты цифровых данных Матрицы, потоки лавы или квантовый шум.
// Параметры:
//   thisdelay (uint8_t) - задержка сдвига регистра
//   thishue   (uint8_t) - базовый оттенок потока
//   mode      (uint8_t) - режим генератора данных:
//                         0 - цифровые пакеты Киберпанк Матрицы (Cyberpunk
//                         Matrix) 1 - тепловой магматический поток (Thermal
//                         Magma stream) 2 - квантовый стохастический шум
//                         (Random Quantum noise)
//   dir       (uint8_t) - направление сдвига регистра: 1 - вправо, 0 - влево
// =============================================================================
void shift_stream() {
  uint8_t mode = mmode;
  uint8_t packetLeft = 0;
  uint8_t gapLeft = 0;
  uint8_t packetLen = random8(3, 7);
  while (check()) {
    TLED inByte = TLED(0, 0, 0);
    if (mode == 0) { // Cyberpunk data packets (Matrix)
      if (packetLeft > 0) {
        inByte = (packetLeft == packetLen) ? TLED(255, 255, 255)
                                           : CHSV(thishue, 240, 200);
        packetLeft--;
        if (packetLeft == 0)
          gapLeft = random8(4, 12);
      } else if (gapLeft > 0) {
        gapLeft--;
      } else {
        packetLen = random8(3, 7);
        packetLeft = packetLen;
      }
    } else if (mode == 1) { // Magma / thermal stream
      inByte = (random8(10) < 3)
                   ? TLED(255, 255, 200)
                   : CHSV(thishue + random8(16), 255, random8(40, 200));
    } else { // Random noise stream
      inByte = rnd();
    }
    leds[0] = inByte;
    if (dir)
      ror(SEG_LEN);
    else
      rol(SEG_LEN);
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 6: radiation (Пульсирующий знак радиации / Кардио-ритм)
// Описание: Пульсирующий строб в виде знака радиационной опасности или
//           двухтактного биения сердца (кардиограммы) с плавной сменой фаз.
// Параметры:
//   thisdelay (uint8_t) - частота пульсаций
//   thishue   (uint8_t) - оттенок свечения (желто-зеленый или красный)
//   opt       (uint8_t) - битовая маска режимов:
//                         бит 2:  заливка всей ленты целиком
//                         бит 0:  режим кардио-пульсации сердца (двойной удар)
//                         бит 3:  альтернативная фаза блоков (чётность)
//                         бит 4:  модуляция насыщенности цветом пульса
// =============================================================================
void radiation() { //-m16-SORT OF RADIATION SYMBOLISH-
  uint8_t isWhole = (opt & 4) ? 1 : 0;
  uint8_t isHeart = (opt & OPT_MODE) ? 1 : 0;
  uint8_t even = (opt & 8) ? 1 : 0;
  uint8_t satMod = (opt & 16) ? 1 : 0;
  uint8_t cnt = random8(1, 10);
  uint16_t w = isWhole ? LED_COUNT : (LED_COUNT / (cnt * 2));
  if (w < 1)
    w = 1;
  uint8_t ibright = 0;
  while (check()) {
    uint8_t br = triwave8(ibright);
    if (isHeart) {
      uint8_t bStep = ibright >> 4;
      br = (bStep < 5) ? triwave8(bStep * 51)
                       : ((bStep < 9) ? (triwave8((bStep - 5) * 64) >> 1) : 0);
    }
    TLED c = CHSV(thishue, satMod ? br : 255, br);
    for (uint16_t i = 0; i < LED_COUNT; i++) {
      if (isWhole || ((i / w) & 1) == even)
        leds[i] = c;
    }
    show_delay(thisdelay);
    if (!++ibright) {
      if (opt & OPT_MODE)
        thishue = random8();
    }
  }
}
// =============================================================================
// Эффект 60: kdv_solitons (Столкновение солитонов Кортевега — де Фриза)
// Описание: Нелинейное волновое уравнение КдВ: два уединенных волновых пакета
//           разной амплитуды движутся навстречу, сталкиваются и проходят
//           сквозь друг друга без искажения формы и скорости.
// Параметры:
//   thisdelay (uint8_t) - скорость фазового движения солитонов
// =============================================================================
void kdv_solitons() {
  int16_t pos1 = 0;
  int16_t pos2 = SYM_SEG_LEN - 1;
  while (check()) {
    fade(160);
    pos1 = (pos1 + 2 >= SYM_SEG_LEN) ? 0 : pos1 + 2;
    pos2 = (pos2 <= 0) ? SYM_SEG_LEN - 1 : pos2 - 1;
    if (abs((int)pos1 - (int)pos2) < 2) {
      int16_t mid = (pos1 + pos2) >> 1;
      if (mid < SYM_SEG_LEN)
        leds[mid] = TLED(255, 255, 255);
    } else {
      if (pos1 < SYM_SEG_LEN)
        leds[pos1] = CHSV(35, 255, 255);
      if (pos2 < SYM_SEG_LEN)
        leds[pos2] = CHSV(135, 255, 200);
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 7: optical_tweezers (Оптический лазерный пинцет)
// Описание: Сфокусированный лазерный луч оптической ловушки захватывает
//           броуновскую микрочастицу градиентной силой светового давления.
// Параметры:
//   thisdelay (uint8_t) - скорость сканирования оптического фокуса
//   thishue   (uint8_t) - цвет лазерного излучения
//   opt       (uint8_t) - битовая маска:
//                         OPT_MODE: 0 - оптический лазерный пинцет,
//                                   1 - биполярная магнитная ловушка
// =============================================================================
void optical_tweezers() {
  int16_t trap = SEG_LEN >> 1;
  int16_t pos = 4;
  int8_t vel = 2;
  uint8_t isMagnetic = opt & OPT_MODE;
  while (check()) {
    fade(175);
    if (isMagnetic) {
      if (trap > 3)
        leds[trap - 4] = CHSV(160, 255, 100);
      if (trap + 4 < SEG_LEN)
        leds[trap + 4] = CHSV(160, 255, 100);
    } else {
      for (int8_t i = -2; i <= 2; i++) {
        int16_t p = trap + i;
        if (p >= 0 && p < SEG_LEN) {
          uint8_t dist = (i < 0) ? -i : i;
          leds[p] = blend(leds[p], CHSV(96, 200, 180 - dist * 50), 120);
        }
      }
    }
    vel += (trap > pos ? 1 : -1) + (int8_t)random8(3) - 1;
    if (vel > 3)
      vel = 3;
    else if (vel < -3)
      vel = -3;

    pos += vel;
    if (pos < 0)
      pos = 0;
    else if (pos >= SEG_LEN)
      pos = SEG_LEN - 1;

    leds[pos] = TLED(255, 255, 255);
    if (pos > 0)
      leds[pos - 1] = CHSV(thishue, 240, 180);
    if (pos < SEG_LEN - 1)
      leds[pos + 1] = CHSV(thishue, 240, 180);

    int16_t diff = trap - pos;
    if (diff >= -1 && diff <= 1 && random8(40) == 0) {
      pos = (random8() & 1) ? 2 : SEG_LEN - 3;
      vel = 0;
      thishue += 45;
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 8: colorWipe (Последовательная сдвиговая заливка ленты)
// Описание: Поочередное непрерывное заполнение всей длины сегмента цветом
//           с последующим заполнением темнотой (стиранием) в выбранном
//           направлении.
// Параметры:
//   thisdelay (uint8_t) - скорость шага заливки
//   thishue   (uint8_t) - цвет заполнения
//   dir       (uint8_t) - направление: 1 - сдвиг вправо, 0 - сдвиг влево
// =============================================================================
void colorWipe() {
  uint8_t step_delay = thisdelay >> 3;
  while (check()) {
    for (uint8_t p = 0; p < 2; p++) {
      for (int16_t i = 0; i < SEG_LEN; i++) {
        leds[0] = (p == 0) ? CHSV(thishue, 255, 255) : TLED(0, 0, 0);
        if (dir)
          ror(SEG_LEN);
        else
          rol(SEG_LEN);
        showseg_delay(step_delay);
        if (!check())
          return;
      }
    }
    thishue += 45;
  }
}
// =============================================================================
// Эффект 9: random_fill (Случайное заполнение пикселей)
// Описание: Случайный выбор погашенных пикселей ленты и их поочередное
// зажигание
//           до 100% заполнения всего сегмента с последующей сменой палитры.
// Параметры:
//   thisdelay (uint8_t) - задержка между зажиганием новых диодов
//   thishue   (uint8_t) - цвет заполняющих пикселей
// =============================================================================
void random_fill() {
  while (check()) {
    TLED target = CHSV(thishue, 255, 255);
    for (int16_t i = 0; i < SEG_LEN; i++) {
      uint16_t idx = random16(0, SEG_LEN);
      leds[idx] = target;
      showseg_delay(thisdelay);
      if (!check())
        return;
    }
    thishue += 60;
  }
}
// =============================================================================
// Эффект 61: Fire (Канонический огонь Fire2012)
// Описание: Знаменитый физический алгоритм симуляции пламени Fire2012 от Марка
//           Кригсмана с матрицей теплопереноса, конвекцией и рождением искр.
// Параметры:
//   SpeedDelay (uint8_t) - задержка кадра анимации огня
//   Cooling    (uint8_t) - скорость охлаждения тепловой дорожки (типично 55)
//   Sparking   (uint8_t) - вероятность зарождения новых очагов пламени (типично
//   120)
// =============================================================================
void Fire() {
  uint8_t SpeedDelay = thisdelay;
  uint8_t Cooling = 55;
  uint8_t Sparking = 120;
  static byte heat[50];
  uint8_t fireLen = min((uint16_t)SYM_SEG_LEN, (uint16_t)50);
  uint8_t max_cooldown = ((uint16_t)Cooling * 10) / fireLen + 2;
  while (check()) {
    // Step 1.  Cool down every cell a little
    for (uint8_t i = 0; i < fireLen; i++) {
      uint8_t cooldown = random8(0, max_cooldown);
      heat[i] = qsub8(heat[i], cooldown);
    }

    // Step 2.  Heat from each cell drifts 'up' and diffuses a little
    for (uint8_t k = fireLen - 1; k >= 2; k--) {
      heat[k] = ((uint16_t)(heat[k - 1] + heat[k - 2] + heat[k - 2]) * 85) >> 8;
    }

    // Step 3.  Randomly ignite new 'sparks' near the bottom
    if (random8() < Sparking) {
      uint8_t heat_y = random8(0, 7);
      heat[heat_y] = qadd8(heat[heat_y], random8(160, 255));
    }

    // Step 4.  Convert heat to LED colors
    for (int16_t j = 0; j < SYM_SEG_LEN; j++) {
      uint8_t hIdx = (j < fireLen) ? j : (fireLen - 1);
      uint8_t t192 = scale8(heat[hIdx], 191);
      uint8_t ramp = (t192 & 0x3F) << 2;
      if (t192 > 0x80)
        leds[j] = TLED(255, 255, ramp);
      else if (t192 > 0x40)
        leds[j] = TLED(255, ramp, 0);
      else
        leds[j] = TLED(ramp, 0, 0);
    }

    showseg_delay(SpeedDelay);
  }
}
// =============================================================================
// Эффект 62: BouncingBalls (Физика прыгающих мячей)
// Описание: Кинематическая симуляция упругих мячей под действием
// гравитационного
//           ускорения свободного падения с затуханием высоты отскока от пола.
// Параметры:
//   BallCount (uint8_t) - количество независимых прыгающих мячей (от 1 до 8)
// =============================================================================
void BouncingBalls() {
  uint8_t BallCount = (random8() >> 5) + 1;
  int16_t pos[8];
  int16_t vel[8];
  TLED ballcolors[8];
  for (uint8_t i = 0; i < BallCount; i++) {
    pos[i] = 0;
    vel[i] = (200 + i * 40);
    ballcolors[i] = rnd();
  }
  while (check()) {
    for (uint8_t i = 0; i < BallCount; i++) {
      pos[i] += vel[i];
      vel[i] -= 8;
      if (pos[i] <= 0) {
        pos[i] = 0;
        vel[i] = -vel[i] - 10;
        if (vel[i] < 40)
          vel[i] = 200 + random8(60);
      }
      uint16_t p = (pos[i] > 0) ? (((uint32_t)pos[i] * 39) >> 8) : 0;
      if (p >= SYM_SEG_LEN)
        p = SYM_SEG_LEN - 1;
      leds[p] = ballcolors[i];
    }
    showseg_delay(15);
    memset8(leds, 0, SYM_SEG_LEN * sizeof(TLED));
  }
}
// =============================================================================
// Эффект 63: fireworks (Праздничный салют / Фейерверк)
// Описание: Ракета со светящимся следом взлетает на заданную высоту и
// взрывается
//           яркой вспышкой с разлетом затухающих гравитационных искр.
// Параметры:
//   thisdelay (uint8_t) - скорость полета ракеты и разлета осколков
//   thishue   (uint8_t) - базовый оттенок вспышки
//   opt       (uint8_t) - битовая маска:
//                         OPT_DIR:  направление взлета ракеты
//                         OPT_MODE: 1 - случайная палитра для каждого взрыва,
//                                   0 - монохромный салют вокруг оттенка
//                                   thishue
// =============================================================================
void fireworks() {
  uint16_t corePos = random16(SYM_SEG_LEN >> 2, ((uint32_t)SYM_SEG_LEN * 3) >> 2);
  uint8_t rocket = opt & OPT_DIR;
  uint16_t rocketPos = 0;
  while (check()) {
    fade(150);
    if (rocket) {
      rocketPos += 3;
      if (rocketPos < corePos) {
        leds[rocketPos] = TLED(255, 255, 200);
        showseg_delay(thisdelay >> 1);
        continue;
      }
      rocketPos = 0;
    }
    corePos = random16(SYM_SEG_LEN >> 2, ((uint32_t)SYM_SEG_LEN * 3) >> 2);
    leds[corePos] = TLED(255, 255, 255);
    for (uint8_t s = 0; s < 5; s++) {
      int p = corePos + random8(13) - 6;
      if (p >= 0 && (uint16_t)p < SYM_SEG_LEN) {
        uint8_t h = (opt & OPT_MODE) ? random8() : (thishue + random8(24));
        leds[p] = (random8(3) == 0) ? TLED(255, 255, 255) : CHSV(h, 220, 255);
      }
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 64: comet (Кометы с соплом Вентури)
// Описание: Одиночные кометы или рой комет со светящейся головой и затухающим
//           хвостом, ускоряющиеся при прохождении через центр ленты (сопло
//           Вентури).
// Параметры:
//   thisdelay (uint8_t) - базовая скорость комет
//   thishue   (uint8_t) - цвет ядра кометы
//   opt       (uint8_t) - битовая маска опций:
//                         бит 4:    1 - одиночная комета, 0 - рой из 1..4 комет
//                         бит 8:    эффект ускорения Вентури в центре ленты
//                         OPT_DIR:  направление полета (вперед / назад)
//                         OPT_MODE: скорость затухания хвоста кометы (165 или
//                         190)
// =============================================================================
void comet() {
  uint8_t count = (opt & 4) ? 1 : random8(1, 5);
  int16_t cpos[4];
  int8_t cspd[4];
  uint8_t deltaHue = 255 / count;
  uint8_t isVenturi = (count == 1) && (opt & 8);
  for (uint8_t k = 0; k < count; k++) {
    cpos[k] = (SYM_SEG_LEN * k) / count;
    cspd[k] = ((opt & OPT_DIR) ? 1 : -1) * (k + 1);
  }
  uint8_t f = (opt & OPT_MODE) ? 165 : 190;
  while (check()) {
    fade(f);
    for (uint8_t k = 0; k < count; k++) {
      int8_t spd = cspd[k];
      if (isVenturi && cpos[k] >= (SYM_SEG_LEN / 3) && cpos[k] <= (SYM_SEG_LEN * 2 / 3))
        spd *= 3;
      cpos[k] += spd;
      if (cpos[k] >= SYM_SEG_LEN)
        cpos[k] -= SYM_SEG_LEN;
      else if (cpos[k] < 0)
        cpos[k] += SYM_SEG_LEN;
      leds[cpos[k]] = (opt & 4) ? TLED(255, 255, 255)
                                : CHSV(thishue + k * deltaHue, 220, 255);
      if (opt & 4) {
        int16_t echo = SYM_SEG_LEN - 1 - cpos[k];
        if (echo >= 0 && echo < SYM_SEG_LEN)
          leds[echo] = CHSV(thishue + cpos[k] * 2, 240, 220);
      }
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 65: boba1 (1D Тетрис / Падающие блоки)
// Описание: Игровая механика 1D Тетриса: блоки падают в стакан, накапливаются у
//           основания и сгорают яркой очищающей строб-вспышкой при заполнении.
// Параметры:
//   del (uint8_t) - скорость падения блоков
//   opt (uint8_t) - битовая маска опций:
//                   бит 2 (dir): направление падения (слева направо или справа
//                   налево) биты 1 и 4:  активация режима со сгоранием
//                   переполненных линий
// =============================================================================
void boba1() {
  uint8_t del = thisdelay & 0x0F;
  TLED c = rnd();
  uint8_t dir = opt & 2;
  uint8_t tet = (opt & 4) && (opt & 1);
  while (check()) {
    for (int i = SYM_SEG_LEN - 1; i > 0; i--) {
      if (opt & 1)
        c = rnd();
      int q = dir ? (i + 1) : (SYM_SEG_LEN - 2 - i);
      if (dir)
        leds[0] = c;
      else
        leds[SYM_SEG_LEN - 1] = c;
      for (int ii = 0; ii < i; ii++) {
        showseg_delay(del);
        if (!check())
          return;
        if (dir) {
          for (int idx = i; idx > 0; idx--)
            leds[idx] = leds[idx - 1];
          leds[0] = 0;
        } else {
          for (int idx = SYM_SEG_LEN - 1 - i; idx < SYM_SEG_LEN - 1; idx++)
            leds[idx] = leds[idx + 1];
          leds[SYM_SEG_LEN - 1] = 0;
        }
      }
      if (tet && q >= 0 && q < SYM_SEG_LEN && leds[q].is_on() &&
          cmp(c, leds[q], 100)) {
        int cur = dir ? i : (SYM_SEG_LEN - 1 - i);
        leds[q] = 0;
        if (cur >= 0 && cur < SYM_SEG_LEN)
          leds[cur] = 0;
        showseg_delay(del);
        i = min(i + 2, (int)SYM_SEG_LEN);
      }
      if (!check())
        return;
    }
    memset8(leds, 0, SYM_SEG_LEN * sizeof(TLED));
  }
}
// =============================================================================
// Эффект 10: theater_chase (Театральный бегущий огонь Бродвея)
// Описание: Классический трехфазный бегущий огонь старинных бродвейских афиш
//           и театральных рамп со сдвигом через каждые 3 пикселя.
// Параметры:
//   thisdelay (uint8_t) - темп переключения фаз бегущего огня
//   thishue   (uint8_t) - цвет огней
//   opt       (uint8_t) - битовая маска опций:
//                         OPT_MODE: 1 - радужный спектральный перелив, 0 -
//                         монохромный OPT_EVEN: инверсия четности фазового
//                         сдвига
// =============================================================================
void theater_chase() {
  while (check()) {
    for (uint8_t q = 0; q < 3; q++) {
      for (int16_t i = 0; i < SEG_LEN; i += 3) {
        if (i + q < SEG_LEN) {
          uint8_t h = (opt & OPT_MODE) ? (thishue + i * 3) : thishue;
          leds[i + q] = CHSV(h, 255, 255);
        }
      }
      showseg_delay(thisdelay);
      memset8(leds, 0, SEG_LEN * sizeof(TLED));
    }
    if (opt & OPT_EVEN)
      thishue += 8;
  }
}
// =============================================================================
// Эффект 66: barcode_scanner (Сканер штрих-кода)
// Описание: Красный лазерный луч считывателя бегает по ленте, сканируя
//           виртуальный штрих-код с короткими вспышками декодированных битов
//           данных.
// Параметры:
//   thisdelay (uint8_t) - скорость движения сканирующего луча
//   thishue   (uint8_t) - спектральный оттенок сканирующего лазера
// =============================================================================
void barcode_scanner() {
  uint8_t barData = random8();
  uint8_t shift = 0;
  TLED onColor = CHSV(thishue, 220, 255);
  while (check()) {
    shift++;
    if (random8(12) == 0) {
      barData = random8();
      thishue += 20;
      onColor = CHSV(thishue, 220, 255);
    }
    for (int16_t i = 0; i < SYM_SEG_LEN; i++) {
      uint8_t bit = (barData >> ((i + shift) & 7)) & 1;
      leds[i] = bit ? onColor : TLED(0, 0, 0);
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 11: pendulum_wave (Массив физических маятников)
// Описание: Симуляция набора связанных математических маятников разной длины:
//           чередование синфазных змеек, стоячих волн и псевдохаотических
//           узоров.
// Параметры:
//   thisdelay (uint8_t) - базовый период колебаний маятников
//   thishue   (uint8_t) - базовый цвет светящихся грузов
// =============================================================================
void pendulum_wave() {
  uint8_t t = 0;
  while (check()) {
    t += 2;
    memset8(leds, 0, SEG_LEN * sizeof(TLED));
    for (int16_t i = 0; i < SEG_LEN; i++) {
      uint8_t phase = ((uint16_t)t * (i + 15)) >> 4;
      uint8_t br = triwave8(phase);
      if (br > 210) {
        leds[i] = CHSV(thishue + i * 4, 220, br);
      }
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 67: cylon_bounce (Глаз Сайлона / Бегущий сканер)
// Описание: Канонический бегущий отражающийся глаз Сайлона (Battlestar
// Galactica)
//           с плавным шлейфом затухания и регулируемой шириной светового ядра.
// Параметры:
//   thisdelay (uint8_t) - скорость перемещения глаза
//   thishue   (uint8_t) - оттенок ядра (классический красный или произвольный)
//   opt       (uint8_t) - битовая маска опций:
//                         OPT_MODE: 1 - два встречных глаза, 0 - одиночный глаз
//                         OPT_HUE:  плавная смена цвета при движении
//                         бит 4:    широкий луч (3..N пикселей) или узкий
//                         точечный OPT_EVEN: размер узкого луча (2 или 1 диод)
// =============================================================================
void cylon_bounce() {
  int16_t p1 = 0, p2 = SYM_SEG_LEN - 1;
  int8_t d1 = 1, d2 = -1;
  uint8_t dual = opt & OPT_MODE;
  uint8_t rainbow = opt & OPT_HUE;
  uint8_t isWide = opt & 4;
  uint8_t eyeSize =
      isWide ? random8(3, (SYM_SEG_LEN >> 3) + 3) : ((opt & OPT_EVEN) ? 2 : 1);
  uint8_t hasSparks = (opt & 8);
  uint8_t eyeStep = 255 / (eyeSize + 1);
  while (check()) {
    fade(195);
    p1 += d1;
    if (p1 <= 0 || p1 >= SYM_SEG_LEN - eyeSize) {
      d1 = -d1;
      if (rainbow)
        thishue += 32;
    }
    for (uint8_t e = 0; e < eyeSize; e++) {
      if (p1 + e < SYM_SEG_LEN) {
        leds[p1 + e] = isWide ? CHSV(thishue, 255, triwave8((e + 1) * eyeStep))
                              : CHSV(thishue, 255, 255);
      }
    }
    if (hasSparks && random8(10) < 5) {
      int16_t sp = p1 + random8(eyeSize);
      if (sp < SYM_SEG_LEN)
        leds[sp] = TLED(255, 255, 255);
    }
    if (dual) {
      p2 += d2;
      if (p2 <= 0 || p2 >= SYM_SEG_LEN - eyeSize)
        d2 = -d2;
      for (uint8_t e = 0; e < eyeSize; e++) {
        if (p2 + e < SYM_SEG_LEN)
          leds[p2 + e] = leds[p2 + e] + CHSV(thishue + 128, 255, 255);
      }
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 12: strobe (Высокоскоростной стробоскоп)
// Описание: Серии коротких пачек ослепительных стробоскопических вспышек
// (bursts)
//           с регулируемой паузой и цветовой модуляцией.
// Параметры:
//   thisdelay (uint8_t) - интервал между сериями вспышек
//   thishue   (uint8_t) - цвет вспышек
//   opt       (uint8_t) - битовая маска опций:
//                         OPT_MODE: 1 - пачки из 2..4 микро-вспышек, 0 -
//                         одиночные вспышки OPT_EVEN: 1 - чистый белый свет, 0
//                         - оттенок thishue OPT_HUE:  случайная смена оттенка
//                         между пачками
// =============================================================================
void strobe() {
  uint8_t bursts = (opt & OPT_MODE) ? random8(2, 5) : 1;
  TLED c = (opt & OPT_EVEN) ? TLED(255, 255, 255) : CHSV(thishue, 255, 255);
  while (check()) {
    for (uint8_t f = 0; f < bursts; f++) {
      fill(0, LED_COUNT, c);
      show_delay(15);
      fill(0, LED_COUNT, TLED(0, 0, 0));
      show_delay(25);
      if (!check())
        return;
    }
    if (opt & OPT_HUE)
      thishue += 64;
    c = (opt & OPT_EVEN) ? TLED(255, 255, 255) : CHSV(thishue, 255, 255);
    show_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 13: police_flasher (Полицейская спецмигалка)
// Описание: Красно-синий стробоскоп оперативных служб: быстрые строб-пачки
// красного
//           на одной половине сегмента чередуются со строб-пачками синего на
//           второй.
// Параметры:
//   thisdelay (uint8_t) - темп переключения стробов (частота мигания)
// =============================================================================
void police_flasher() {
  uint8_t pStep = 0;
  int16_t half = SEG_LEN >> 1;
  while (check()) {
    fill(0, SEG_LEN, TLED(0, 0, 0));
    uint8_t isLeft = (pStep & 2) == 0;
    uint8_t isFlash = (pStep & 1) == 0;
    if (isFlash) {
      if (isLeft)
        fill(0, half, TLED(255, 0, 0));
      else
        fill(half, SEG_LEN - half, TLED(0, 0, 255));
    }
    pStep = (pStep + 1) & 7;
    showseg_delay(thisdelay >> 3);
  }
}
// =============================================================================
// Эффект 14: morse_code (Оптический телеграф Морзе)
// Описание: Передача кодированных сообщений азбукой Морзе вспышками света
//           с соблюдением стандартных длительностей точек, тире и пауз между
//           знаками.
// Параметры:
//   thisdelay (uint8_t) - скорость передачи кода (длина элементарной точки)
//   thishue   (uint8_t) - цвет передающего светодиода
// =============================================================================
void morse_code() {
  uint16_t pattern = 0xAEBB;
  uint8_t mBit = 0;
  while (check()) {
    bool on = (pattern >> mBit) & 1;
    fill(0, LED_COUNT, on ? CHSV(thishue, 240, 255) : TLED(0, 0, 0));
    mBit = (mBit + 1) & 0x0F;
    if (mBit == 0)
      thishue += 37;
    show_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 15: flicker_lamp (Неисправная люминесцентная лампа)
// Описание: Реалистичная физическая симуляция старой люминесцентной лампы
//           со случайным искрением стартера, дребезгом контактов и провалами
//           накала.
// Параметры:
//   thisdelay (uint8_t) - базовый темп переключения состояний лампы
//   thishue   (uint8_t) - спектральная температура газового разряда
// =============================================================================
void flicker_lamp() {
  TLED c = CHSV(thishue, 100, 255);
  while (check()) {
    fill(0, LED_COUNT, c);
    show_delay(thisdelay);
    if (!check())
      return;
    if (random8(10) < 4) {
      uint8_t blinks = random8(2, 6);
      for (uint8_t b = 0; b < blinks; b++) {
        fill(0, LED_COUNT, (b & 1) ? TLED(0, 0, 0) : c);
        show_delay(random8(10, 30));
        if (!check())
          return;
      }
    }
  }
}
// =============================================================================
// Эффект 16: sparkles (Поле искр и дуговые разряды Теслы)
// Описание: Поле хаотично вспыхивающих и затухающих искр с периодическими
//           кластерными микро-дугами электрического разряда катушки Теслы.
// Параметры:
//   thisdelay (uint8_t) - скорость затухания искр
//   thishue   (uint8_t) - базовый цвет искр
//   opt       (uint8_t) - битовая маска опций:
//                         OPT_MODE: скорость затухания следа (быстрая 170 /
//                         мягкая 210) бит 8:    генерация кластерных
//                         высоковольтных дуг OPT_HUE:  радужные искры или
//                         монохромная палитра
// =============================================================================
void sparkles() {
  uint8_t fadeRate = (opt & OPT_MODE) ? 170 : 210;
  uint8_t isTesla = opt & 8;
  uint8_t isRainbow = opt & OPT_HUE;
  while (check()) {
    fade(fadeRate);
    if (isTesla) {
      uint8_t sparks = random8(2, 6);
      int16_t p = random16(0, LED_COUNT);
      leds[p] = TLED(255, 255, 255);
      for (uint8_t s = 0; s < sparks; s++) {
        int16_t branch = constrain(p + (int8_t)random8(7) - 3, 0, LED_COUNT - 1);
        leds[branch] = CHSV(thishue + random8(20), random8(120, 220), 255);
      }
    } else {
      uint8_t sparkCount = random8(1, 4);
      for (uint8_t s = 0; s < sparkCount; s++) {
        uint16_t pos = random16(0, LED_COUNT);
        if (!isRainbow) {
          leds[pos] =
              (random8(3) == 0) ? TLED(255, 255, 255) : CHSV(thishue, 140, 255);
        } else {
          leds[pos] = leds[pos] + CHSV(random8(), 240, 255);
        }
      }
    }
    show_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 19: wandering_lights (Блуждающие бильярдные частицы)
// Описание: Симуляция упругих световых частиц с ненулевой массой, двигающихся
//           с разной скоростью и упруго отражающихся от границ сегмента ленты.
// Параметры:
//   thisdelay (uint8_t) - скорость перемещения частиц
//   thishue   (uint8_t) - цвет световых шаров
//   opt       (uint8_t) - битовая маска:
//                         OPT_MODE: 1 - 2 частицы на сегмент, 0 - 4 частицы
// =============================================================================
void wandering_lights() {
  uint8_t count = (opt & OPT_MODE) ? 2 : 4;
  int16_t pos[4];
  int8_t speed[4] = {1, -1, 1, -1};
  uint8_t deltaHue = 255 / count;
  for (uint8_t i = 0; i < count; i++)
    pos[i] = (int16_t)(((uint16_t)SEG_LEN * i) / count);
  while (check()) {
    fade(185);
    for (uint8_t i = 0; i < count; i++) {
      pos[i] += speed[i];
      if (pos[i] <= 0 || pos[i] >= SEG_LEN - 1)
        speed[i] = -speed[i];
      leds[pos[i]] = leds[pos[i]] + CHSV(thishue + i * deltaHue, 220, 255);
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 20: hydraulic_jump (Гидравлический скачок потока жидкости)
// Описание: Моделирование гидродинамического скачка при переходе
// сверхкритического
//           потока жидкости в спокойный с образованием турбулентного пенного
//           вала.
// Параметры:
//   thisdelay (uint8_t) - скорость набегающего потока
//   thishue   (uint8_t) - цвет жидкости и аэрированной пены
// =============================================================================
void hydraulic_jump() {
  int16_t jumpPos = 0;
  int dir = 1;
  while (check()) {
    fade(185);
    jumpPos += dir;
    if (jumpPos >= SEG_LEN - 4 || jumpPos <= 0)
      dir = -dir;
    for (int16_t i = 0; i < jumpPos; i++) {
      leds[i] = CHSV(thishue, 240, 80);
    }
    leds[jumpPos] = TLED(255, 255, 255);
    if (jumpPos + 1 < SEG_LEN)
      leds[jumpPos + 1] = CHSV(thishue + 20, 180, 240);
    for (int16_t i = jumpPos + 2; i < SEG_LEN; i++) {
      leds[i] = CHSV(thishue, 255, 180);
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 21: solar_wind (Солнечный ветер и вспышки короны)
// Описание: Истечение высокоскоростной плазмы из центра звезды (корональные
// выбросы
//           массы) с ионизацией окружающего межпланетного пространства.
// Параметры:
//   thisdelay (uint8_t) - скорость радиального потока солнечного ветра
//   thishue   (uint8_t) - оттенок коронального излучения звезды
// =============================================================================
void solar_wind() {
  int16_t planet = ((uint16_t)SEG_LEN * 85) >> 8;
  uint8_t t = 0;
  while (check()) {
    t += 4;
    fade(160);
    leds[planet] = TLED(0, 180, 255);
    int8_t shock = planet - 3;
    if (shock >= 0)
      leds[shock] = TLED(255, 255, 200);
    int plasmaPos = (t * 2) % (planet);
    leds[plasmaPos] = CHSV(thishue, 255, 255);
    for (int16_t i = planet + 1; i < SEG_LEN; i++) {
      uint8_t tailBr = qsub8(240, (i - planet) * 12);
      if (tailBr > 0)
        leds[i] = CHSV(thishue + 64, 220, tailBr);
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 22: gravitational_lensing (Гравитационное линзирование черной дыры)
// Описание: Релятивистское отклонение лучей света фонового звездного неба
// гравитацией
//           черной дыры с образованием кольца Эйнштейна вокруг горизонта
//           событий.
// Параметры:
//   thisdelay (uint8_t) - скорость дрейфа фонового звездного поля
//   thishue   (uint8_t) - спектральный класс фоновых звезд
// =============================================================================
void gravitational_lensing() {
  int16_t center = SEG_LEN >> 1;
  int16_t starPos = 0;
  while (check()) {
    fade(190);
    starPos = (starPos + 1) % SEG_LEN;
    leds[center] = TLED(0, 0, 0);
    if (center > 0)
      leds[center - 1] = TLED(10, 0, 20);
    if (center < SEG_LEN - 1)
      leds[center + 1] = TLED(10, 0, 20);

    uint16_t d = (starPos > center) ? (starPos - center) : (center - starPos);
    if (d > 4) {
      leds[starPos] = CHSV(thishue, 180, 200);
    } else {
      uint8_t caustBr = 255 - d * 40;
      int16_t c1 = center - (5 - d);
      int16_t c2 = center + (5 - d);
      if (c1 >= 0)
        leds[c1] = TLED(caustBr, caustBr, 255);
      if (c2 < SEG_LEN)
        leds[c2] = TLED(caustBr, caustBr, 255);
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 25: quantum_chaos (Квантовый хаос волнового пакета)
// Описание: Нелинейная эволюция квантового состояния с дискретной
// интерференцией
//           бит (XOR-интерференция), генерирующая самоподобные хаотические
//           фракталы.
// Параметры:
//   thisdelay (uint8_t) - темп квантовых скачков
//   thishue   (uint8_t) - базовый квантовый спектр
// =============================================================================
void quantum_chaos() {
  uint8_t psi = 0;
  while (check()) {
    psi += 7;
    fade(160);
    for (int16_t i = 0; i < SEG_LEN; i++) {
      uint8_t p = triwave8((i * 13 + psi) ^ (i * 7 - psi));
      if (p > 220) {
        leds[i] = (p > 245) ? TLED(255, 255, 255)
                            : CHSV(thishue + (p >> 1), 220, 255);
      }
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 17: resonant_wave (Интерференция резонансных волн)
// Описание: Интерференция двух встречных гармонических волн разной частоты
//           с образованием узлов и пучностей стоячей волны и цветовым
//           кодированием фазы.
// Параметры:
//   thisdelay (uint8_t) - частота бегущих волн
//   thishue   (uint8_t) - базовый оттенок интерференционной картины
//   opt       (uint8_t) - битовая маска опций:
//                         OPT_DIR: подсветка узловых точек резонанса белым
//                         цветом
// =============================================================================
void resonant_wave() {
  uint8_t t = 0;
  uint8_t freq = random8(10, 24);
  uint8_t hue2 = thishue + 128;
  while (check()) {
    t += 4;
    for (int16_t i = 0; i < SEG_LEN; i++) {
      uint8_t s1 = triwave8((i * freq + t) & 0xFF);
      uint8_t s2 = triwave8((i * freq - t) & 0xFF);
      if ((opt & OPT_DIR) && abs_diff8(s1, s2) < 14)
        leds[i] = TLED(255, 255, 255);
      else
        leds[i] = (s1 > s2) ? CHSV(thishue, 255, s1) : CHSV(hue2, 255, s2);
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 18: acoustic_resonance (Акустический резонанс)
// Описание: Периодические акустические импульсы давления, распространяющиеся
//           из центра ленты к границам с затуханием амплитуды звука в среде.
// Параметры:
//   thisdelay (uint8_t) - скорость звука (задержка распространения волны)
//   thishue   (uint8_t) - спектральный цвет звуковой волны
// =============================================================================
void acoustic_resonance() {
  uint8_t t = 0;
  int16_t center = SEG_LEN >> 1;
  uint8_t freq = random8(10, 24);
  while (check()) {
    t += 4;
    fade(175);
    for (int16_t i = 0; i < center; i++) {
      uint8_t amp = triwave8((i * freq - t) & 0xFF);
      uint8_t v = (amp > 150) ? amp : 0;
      TLED c = CHSV(thishue + (i << 2), 220, v);
      if (center - 1 - i >= 0)
        leds[center - 1 - i] = leds[center - 1 - i] + c;
      if (center + i < SEG_LEN)
        leds[center + i] = leds[center + i] + c;
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 26: bubble_sort (Визуализация сортировки пузырьком)
// Описание: Наглядная демонстрация алгоритма сортировки пузырьком: случайно
//           перемешанный спектр ленты шаг за шагом выстраивается в идеальную
//           радугу.
// Параметры:
//   thisdelay (uint8_t) - задержка каждого шага сравнения и перестановки пар
//   thishue   (uint8_t) - начальный фазовый сдвиг перемешиваемой палитры
// =============================================================================
void bubble_sort() {
  for (int16_t i = 0; i < SEG_LEN; i++)
    leds[i] = CHSV(thishue + (random8() >> 2), 240, random8(40, 255));
  int16_t ptr = 0;
  while (check()) {
    if (leds[ptr].get_r() > leds[ptr + 1].get_r()) {
      TLED tmp = leds[ptr];
      leds[ptr] = leds[ptr + 1];
      leds[ptr + 1] = tmp;
    }
    ptr++;
    if (ptr >= SEG_LEN - 1) {
      ptr = 0;
      if (random8(10) == 0) {
        for (int16_t i = 0; i < SEG_LEN; i++)
          leds[i] = CHSV(thishue + (random8() >> 2), 240, random8(40, 255));
        thishue += 35;
      }
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 68: wave_synthesis (3-волновой синтезатор палитр)
// Описание: Трехволновой интерференционный синтезатор гармоник с наложением
//           сложных спектральных палитр (Аврора, Лава, Водные каустики,
//           Pacifica).
// Параметры:
//   thisdelay (uint8_t) - скорость наложения волн
//   thishue   (uint8_t) - базовый цветовой тон
//   mode      (uint8_t) - выбор синтезируемой темы палитры (0..6):
//                         0 - Северное сияние (Aurora Borealis)
//                         1 - Лавовый поток (Magma flow)
//                         2 - Океаническая лазурь (Pacifica blue)
//                         3 - RGB спектральный калейдоскоп
//                         4 - Водные каустики (Water caustics)
//                         5 - Неоновый закат (Neon Sunset)
//                         6 - Золотая осень (Golden Autumn)
// =============================================================================
void wave_synthesis() {
  uint8_t mode = random8(7);
  uint8_t t1 = 0, t2 = 0, t3 = 0;
  while (check()) {
    t1 += 2;
    t2 += 3;
    t3 += 4;
    for (int16_t i = 0; i < SYM_SEG_LEN; i++) {
      uint8_t w1 = triwave8((i * 12 + t1) & 0xFF);
      uint8_t w2 = triwave8((i * 18 - t2) & 0xFF);
      uint8_t w3 = triwave8((i * 8 + t3) & 0xFF);
      uint8_t avg12 = (w1 + w2) >> 1;
      TLED c(0, 0, 0);
      if (mode == 0) {
        c = CHSV(thishue + ((i * 2) & 0xFF), 240, w1);
      } else if (mode == 1) {
        c = CHSV(95 + (avg12 >> 2), 240, triwave8((w1 + w2) & 0xFF));
      } else if (mode == 2) {
        c = CHSV((avg12 > 160) ? ((avg12 - 160) >> 2) : 0, 255, avg12);
      } else if (mode == 3) {
        uint8_t br = ((uint16_t)(w1 + w2 + w3) * 85) >> 8;
        c = CHSV(130 + (avg12 >> 2), 240, br);
        if (w1 > 240 && w2 > 230)
          c = blend(c, TLED(255, 255, 255), 180);
      } else if (mode == 4) {
        c = TLED(qsub8(w1, 60), qsub8(w2, 70), qsub8(w3, 60));
      } else if (mode == 5) {
        // Неоновый закат: тёплый градиент от оранжевого к маджентовому.
        c = CHSV(10 + (w1 >> 4), 245, avg12);
      } else if (mode == 6) {
        // Золотая осень: янтарные волны с редкими белыми бликами.
        c = (w1 > 220 && w2 > 220) ? TLED(255, 255, 255)
                                   : CHSV(28 + (w2 >> 4), 240, avg12);
      } else {
        c = CHSV(thishue + (w1 >> 3), 230, avg12);
      }
      leds[i] = (mode == 3) ? blend(leds[i], c, 120) : c;
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 27: rule30_ca (Клеточный автомат Правило 30)
// Описание: Знаменитый одномерный клеточный автомат Стивена Вольфрама (Правило
// 30),
//           порождающий апериодический детерминированный хаос из простых
//           правил.
// Параметры:
//   thisdelay (uint8_t) - темп смены поколений клеточного автомата
//   thishue   (uint8_t) - цвет живых клеток автомата
// =============================================================================
void rule30_ca() {
  fill(0, SEG_LEN, TLED(0, 0, 0));
  leds[SEG_LEN >> 1] = CHSV(thishue, 255, 255);
  while (check()) {
    uint8_t prev = leds[0].is_on();
    uint8_t mid = leds[1].is_on();
    for (int16_t i = 1; i < SEG_LEN - 1; i++) {
      uint8_t right = leds[i + 1].is_on();
      uint8_t state = prev ^ (mid | right);
      prev = mid;
      mid = right;
      leds[i] = state ? CHSV(thishue, 255, 255) : TLED(0, 0, 0);
    }
    thishue += 2;
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 23: flame_ambience (Уютный очаг / Дыхание камина)
// Описание: Мягкое мерцающее тепло домашнего камина с плавной пульсацией жара
//           и редким вылетом растрескивающихся ярких угольков.
// Параметры:
//   thisdelay (uint8_t) - темп дыхания пламени
//   thishue   (uint8_t) - базовый оттенок
//   opt       (uint8_t) - битовая маска опций:
//                         OPT_EVEN: 1 - оттенок thishue, 0 - канонический
//                         теплый древесный огонь (hue 25) OPT_DIR:
//                         периодический треск с выбросом ярких искр
// =============================================================================
void flame_ambience() {
  uint8_t flameHue = (opt & OPT_EVEN) ? thishue : 25;
  int16_t center = SEG_LEN >> 1;
  uint8_t heat = 220;
  while (check()) {
    if ((opt & OPT_DIR) && random8(30) == 0)
      heat = 35;
    else if (heat < 220)
      heat += 25;
    else
      heat = 210 + random8(35);
    fade(170);
    if (opt & OPT_DIR) {
      leds[center] = CHSV(flameHue, 240, heat);
      if (center > 0)
        leds[center - 1] = CHSV(flameHue, 240, heat >> 1);
      if (center < SEG_LEN - 1)
        leds[center + 1] = CHSV(flameHue, 240, heat >> 1);
    } else {
      for (int16_t i = 0; i < SEG_LEN; i++) {
        leds[i] = CHSV(flameHue, 240, heat - random8(20));
      }
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 24: wandering_tongue (Ползающий язык пламени)
// Описание: Одиночный живой язычок пламени ползает по ленте вперед и назад
//           с градиентным раскаленным ядром и плавно гаснущим теплым хвостом.
// Параметры:
//   thisdelay (uint8_t) - скорость перемещения пламени
//   thishue   (uint8_t) - цветовая температура пламени
// =============================================================================
void wandering_tongue() {
  int16_t tonguePos = 0;
  int8_t dir = 1;
  while (check()) {
    fade(190);
    tonguePos += dir * 2;
    if (tonguePos >= SEG_LEN || tonguePos < 0) {
      dir = -dir;
      tonguePos = constrain(tonguePos, 0, SEG_LEN - 1);
    }
    for (int16_t i = 0; i < SEG_LEN; i++) {
      if (random8(10) < 3)
        leds[i] = CHSV(thishue, 255, random8(80, 160));
    }
    for (uint8_t i = 0; i < 5; i++) {
      int16_t p = tonguePos + i * dir;
      if (p >= 0 && p < SEG_LEN) {
        leds[p] = (i == 0) ? TLED(255, 255, 255) : CHSV(thishue + 15, 200, 255);
      }
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 69: gravitational_fall (Гравитационное падение с ударом)
// Описание: Падение светящихся сгустков с физическим квадратичным ускорением g,
//           заканчивающееся вспышкой кинетического удара об основание.
// Параметры:
//   thisdelay (uint8_t) - шаг гравитационного таймера
//   thishue   (uint8_t) - цвет падающих сгустков
//   opt       (uint8_t) - битовая маска:
//                         OPT_MODE: 1 - падение к центру ленты, 0 - падение к
//                         краю
// =============================================================================
void gravitational_fall() {
  uint8_t isCenter = opt & OPT_MODE;
  int16_t apex = isCenter ? (SYM_SEG_LEN >> 1) : (SYM_SEG_LEN - 1);
  int16_t p = 0;
  while (check()) {
    fade(165);
    p++;
    if (p >= apex) {
      leds[apex] = TLED(255, 255, 255);
      p = 0;
      thishue += 35;
    } else {
      leds[p] = CHSV(thishue, 220, 255);
      if (isCenter)
        leds[SYM_SEG_LEN - 1 - p] = CHSV(thishue + 128, 220, 255);
    }
    // Задержка зависит от оставшегося пути, но всегда укладывается в байт:
    // нормируем путь на длину сегмента и насыщающе складываем с thisdelay.
    uint8_t slowdown =
        (uint8_t)(((uint32_t)(apex - p) * 127) / SYM_SEG_LEN);
    showseg_delay(qadd8(thisdelay, slowdown));
  }
}
// =============================================================================
// Эффект 28: kaleidoscope (Оптический калейдоскоп)
// Описание: Вращение многоцветных световых гармоник с многократным зеркальным
//           отражением относительно осей симметрии, создающее завораживающий
//           узор.
// Параметры:
//   thisdelay (uint8_t) - скорость вращения призм калейдоскопа
//   thishue   (uint8_t) - спектральная палитра узоров
// =============================================================================
void kaleidoscope() {
  uint8_t phase = 0;
  int16_t qtr = SEG_LEN >> 2;
  if (qtr < 1)
    qtr = 1;
  int16_t q2 = qtr << 1;
  int16_t q4 = qtr << 2;
  while (check()) {
    phase += 3;
    for (int16_t i = 0; i < qtr; i++) {
      uint8_t br = triwave8((i * 24 + phase) & 0xFF);
      TLED c = CHSV(thishue + i * 8, 255, br);
      leds[i] = c;
      if (q2 - 1 - i < SEG_LEN)
        leds[q2 - 1 - i] = c;
      if (q2 + i < SEG_LEN)
        leds[q2 + i] = c;
      if (q4 - 1 - i < SEG_LEN)
        leds[q4 - 1 - i] = c;
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 29: pacman_run (Игра Pacman / Пакман и привидение)
// Описание: 1D аркада Pacman: желтый Пакман бежит по ленте, поедая точки;
//           за ним гонится цветное привидение. В режиме энерджайзера Пакман сам
//           атакует синее испуганное привидение.
// Параметры:
//   thisdelay (uint8_t) - скорость бега персонажей
//   opt       (uint8_t) - битовая маска:
//                         OPT_MODE: 1 - режим энерджайзера (Пакман охотится за
//                         привидением) OPT_DIR:  направление движения
//                         персонажей
// =============================================================================
void pacman_run() {
  int pacPos = 0;
  int dir = 1;
  uint8_t energizer = opt & OPT_MODE;
  while (check()) {
    fill(0, SEG_LEN, TLED(0, 0, 0));
    for (int16_t i = 0; i < SEG_LEN; i += 5) {
      if (dir > 0 ? (i > pacPos) : (i < pacPos)) {
        leds[i] = TLED(30, 25, 10);
      }
    }
    leds[pacPos] = TLED(255, 200, 0);
    for (uint8_t k = 1; k <= 4; k++) {
      int g = pacPos - (k << 2) * dir;
      if (g >= 0 && g < SEG_LEN) {
        if (energizer) {
          leds[g] = ((pacPos >> 1) & 1) ? TLED(0, 0, 255) : TLED(200, 200, 255);
        } else {
          leds[g] = CHSV(k * 45, 240, 255);
        }
      }
    }
    pacPos += dir;
    if (pacPos >= SEG_LEN - 1 || pacPos <= 0) {
      dir = -dir;
      energizer = !energizer;
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 30: pride2015 (Легендарная палитра Pride 2015)
// Описание: Шедевр Марка Кригсмана: многослойное наложение нескольких радужных
//           волн разной пространственной частоты и скорости без единого шва.
// Параметры:
//   thisdelay (uint8_t) - задержка между шагами волновых слоев
// =============================================================================
void pride2015() {
  uint16_t sPseudotime = 0;
  uint16_t sHue16 = 0;
  while (check()) {
    uint8_t sat8 = 220 + scale8(triwave8((sPseudotime >> 6) & 0xFF), 30);
    uint8_t brightdepth = 96 + scale8(triwave8((sPseudotime >> 7) & 0xFF), 128);
    uint16_t brightnessthetainc16 =
        25 * 256 + (triwave8((sPseudotime >> 8) & 0xFF) * 15);
    sPseudotime += 40;
    sHue16 += 8;
    uint16_t hue16 = sHue16;
    uint16_t brightnesstheta16 = sPseudotime;
    for (int16_t i = 0; i < LED_COUNT; i++) {
      hue16 += 1200;
      brightnesstheta16 += brightnessthetainc16;
      uint8_t b8 = triwave8((brightnesstheta16 >> 8) & 0xFF);
      uint8_t bri8 = scale8(b8, brightdepth) + (255 - brightdepth);
      TLED newc = CHSV(hue16 >> 8, sat8, bri8);
      leds[i] = blend(leds[i], newc, 64);
    }
    show_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 31: blendwave (Плавное смешивание палитр)
// Описание: Две встречные волны плавно смешивают противоположные или
// тематические
//           цвета палитры (комплементарные цвета, Хэллоуин, Лед и пламя).
// Параметры:
//   thisdelay (uint8_t) - скорость распространения волн смешивания
//   thishue   (uint8_t) - базовый оттенок
//   opt       (uint8_t) - выбор тематического подрежима (opt & 3):
//                         0 - комплементарная контрастная палитра (hue /
//                         hue+128) 1 - Хэллоуин (оранжевый / фиолетовый) 2 -
//                         Лед и пламя (голубой / огненно-красный) 3 -
//                         Пульсирующая яркость
// =============================================================================
void blendwave() {
  uint8_t phase = 0;
  uint8_t mode = opt & 3; // 0=complementary, 1=halloween orange/purple,
                          // 2=police red/blue, 3=breathing
  while (check()) {
    phase += 3;
    int16_t loc = (((uint32_t)triwave8(phase) * (SEG_LEN - 1)) >> 8);
    TLED clr1(0, 0, 0), clr2(0, 0, 0);
    if (mode == 1) {
      clr1 = CHSV(20, 240, 255);  // pumpkin orange
      clr2 = CHSV(190, 240, 255); // eerie purple
    } else if (mode == 2) {
      clr1 = TLED(255, 0, 0); // red
      clr2 = TLED(0, 0, 255); // blue
    } else {
      uint8_t br = (mode == 3) ? triwave8(phase) : 255;
      clr1 = CHSV(thishue, 255, br);
      clr2 = CHSV(thishue + 128, 255, 255 - br);
    }
    for (int16_t i = 0; i < SEG_LEN; i++) {
      uint8_t amt = (i < loc)
                        ? ((uint16_t)i * 255 / (loc + 1))
                        : ((uint16_t)(SEG_LEN - 1 - i) * 255 / (SEG_LEN - loc));
      leds[i] = blend(clr1, clr2, amt);
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 70: halloween_eyes (Глаза в темноте)
// Описание: В темноте неожиданно загораются пары жутких светящихся глаз,
//           наблюдают за окружением, несколько раз моргают и растворяются во
//           тьме.
// Параметры:
//   thisdelay (uint8_t) - скорость моргания и появления новых глаз
// =============================================================================
void halloween_eyes() {
  uint16_t eyePos = random16(2, SYM_SEG_LEN - 6);
  TLED eyeColor = CHSV((random8() & 1) ? 0 : 30, 255, 255);
  uint8_t step = 0;
  while (check()) {
    memset8(leds, 0, SYM_SEG_LEN * sizeof(TLED));
    if (++step >= 18) {
      step = 0;
      eyePos = random16(2, SYM_SEG_LEN - 6);
      eyeColor = CHSV((random8() & 1) ? 0 : 30, 255, 255);
    } else {
      uint8_t br =
          (step < 6) ? (step * 45) : ((step < 12) ? 255 : (18 - step) * 45);
      TLED c = blend(TLED(0, 0, 0), eyeColor, br);
      uint16_t ep = eyePos + ((step >= 6 && step < 12 && (step & 1)) ? 1 : 0);
      leds[ep] = c;
      leds[ep + 3] = c;
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 32: twinkle_fox (Рождественские мерцания TwinkleFox)
// Описание: Канонический алгоритм мягких псевдослучайных мерцаний TwinkleFox
//           от Марка Кригсмана: плавное несинхронное зажигание и таяние звезд.
// Параметры:
//   thisdelay (uint8_t) - скорость цикла мерцаний
//   thishue   (uint8_t) - базовая палитра мерцающих звезд
// =============================================================================
void twinkle_fox() {
  uint16_t tick = 0;
  while (check()) {
    tick += 3;
    uint16_t seed = 42;
    for (uint16_t i = 0; i < LED_COUNT; i++) {
      seed = (seed * 2053u) + 13849u;
      uint8_t initVal = (seed + (seed >> 8)) & 0xFF;
      seed = (seed * 2053u) + 13849u;
      uint8_t speed = (((seed >> 8) & 0x07) + 1) * 2;
      uint8_t phase = initVal + (tick * speed);
      uint8_t br = triwave8(phase);
      if (br > 140) {
        leds[i] = CHSV(thishue + (initVal >> 2), 200, br);
      } else {
        leds[i] = TLED(0, 0, 0);
      }
    }
    show_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 71: shockwave (Взрывная ударная волна)
// Описание: Точечный эпицентр взрыва порождает сверхзвуковую ударную волну
//           с ослепительным фронтом высокого давления и затухающим шлейфом.
// Параметры:
//   thisdelay (uint8_t) - скорость расширения фронта ударной волны
//   thishue   (uint8_t) - цвет раскаленной плазмы фронта
// =============================================================================
void shockwave() {
  int16_t epic = SYM_SEG_LEN >> 1;
  int16_t r = 0;
  uint8_t phase = 0;
  while (check()) {
    if (phase == 0) {
      fade(170);
      leds[epic] = TLED(255, 255, 255);
      if (++r >= 10) {
        phase = 1;
        r = 0;
      }
    } else {
      fade(140);
      if (epic - r >= 0)
        leds[epic - r] =
            (r < 3) ? TLED(255, 255, 255) : CHSV(thishue, 240, 255);
      if (epic + r < SYM_SEG_LEN)
        leds[epic + r] =
            (r < 3) ? TLED(255, 255, 255) : CHSV(thishue, 240, 255);
      if (++r > epic) {
        phase = 0;
        r = 0;
        thishue += 45;
      }
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 33: seismic_waves (Сейсмические волны землетрясения)
// Описание: Моделирование распространения сейсмических волн: быстрая продольная
//           P-волна сжатия опережает мощную разрушительную поперечную S-волну
//           сдвига.
// Параметры:
//   thisdelay (uint8_t) - скорость движения геологических волн
//   thishue   (uint8_t) - цвет сейсмографического следа
// =============================================================================
void seismic_waves() {
  int16_t epic = SEG_LEN >> 1;
  int16_t pPos = 0, sPos = 0;
  while (check()) {
    fade(160);
    pPos += 3;
    sPos += 1;
    if (sPos > epic) {
      pPos = sPos = 0;
      epic = random16(SEG_LEN >> 2, ((uint32_t)SEG_LEN * 3) >> 2);
    }
    for (uint8_t k = 0; k < 2; k++) {
      int16_t p = (k == 0) ? (epic - pPos) : (epic + pPos);
      if (p >= 0 && p < SEG_LEN)
        leds[p] = TLED(200, 240, 255);
      int16_t s = (k == 0) ? (epic - sPos) : (epic + sPos);
      if (s >= 0 && s < SEG_LEN)
        leds[s] = CHSV(thishue, 255, 255);
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 34: sonoluminescence (Сонолюминесценция)
// Описание: Акустическое сжатие микропузырька в ультразвуковом поле жидкости:
//           схлопывание порождает пикосекундную ослепительную вспышку света.
// Параметры:
//   thisdelay (uint8_t) - частота акустической кавитации
//   thishue   (uint8_t) - цвет ионизированного свечения каверны
// =============================================================================
void sonoluminescence() {
  int16_t epic = SEG_LEN >> 1;
  int16_t r = 0;
  uint8_t collapsing = 0;
  while (check()) {
    fade(175);
    if (!collapsing) {
      if (++r >= 8)
        collapsing = 1;
    } else {
      if (r > 1) {
        r -= 2;
      } else {
        leds[epic] = TLED(255, 255, 255);
        showseg_delay(20);
        collapsing = 0;
        r = 1;
        thishue += 30;
      }
    }
    for (uint8_t i = 0; i <= r; i++) {
      TLED c = CHSV(thishue + (collapsing ? 140 : 110), 200, 50 + i * 20);
      if (epic - i >= 0)
        leds[epic - i] = c;
      if (epic + i < SEG_LEN)
        leds[epic + i] = c;
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 35: magnetic_reconnection (Магнитное пересоединение)
// Описание: Взрывное пересоединение силовых линий магнитного поля с
// аннигиляцией
//           магнитной энергии и выбросом релятивистских плазменных джетов.
// Параметры:
//   thisdelay (uint8_t) - скорость сближения магнитных полюсов
//   thishue   (uint8_t) - цвет ускоряемых плазменных струй
// =============================================================================
void magnetic_reconnection() {
  int16_t center = SEG_LEN >> 1;
  int16_t inflow = center >> 1;
  uint8_t snapping = 0;
  int16_t outflow = 0;
  while (check()) {
    fade(165);
    if (!snapping) {
      inflow--;
      if (center - inflow >= 0)
        leds[center - inflow] = CHSV(0, 255, 220);
      if (center + inflow < SEG_LEN)
        leds[center + inflow] = CHSV(160, 255, 220);
      if (inflow <= 1) {
        snapping = 1;
        outflow = 0;
        leds[center] = TLED(255, 255, 255);
      }
    } else {
      outflow += 2;
      TLED jetCol = CHSV(40, 220, 255);
      if (center - outflow >= 0)
        leds[center - outflow] = jetCol;
      if (center + outflow < SEG_LEN)
        leds[center + outflow] = jetCol;
      if (outflow >= center) {
        snapping = 0;
        inflow = center >> 1;
        thishue += 35;
      }
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 36: percolation_breakdown (Перколяционный пробой)
// Описание: Стохастический электрический пробой твердого диэлектрика:
// проводящие
//           фрактальные каналы пробивают изолятор до полного замыкания
//           электродов.
// Параметры:
//   thisdelay (uint8_t) - скорость разрастания проводящего кластера
//   thishue   (uint8_t) - цвет электрических микро-разрядов
// =============================================================================
void percolation_breakdown() {
  memset8(leds, 0, SEG_LEN * sizeof(TLED));
  int16_t sparks = 0;
  while (check()) {
    if (sparks < (SEG_LEN / 3)) {
      int p = random16(0, SEG_LEN);
      leds[p] = CHSV(thishue + random8(32), 200, 140);
      sparks++;
      showseg_delay(thisdelay);
    } else {
      fill(0, SEG_LEN, TLED(255, 255, 255));
      showseg_delay(35);
      if (!check())
        return;
      fill(0, SEG_LEN, CHSV(180, 240, 255));
      showseg_delay(20);
      memset8(leds, 0, SEG_LEN * sizeof(TLED));
      sparks = 0;
      thishue += 45;
    }
  }
}
// =============================================================================
// Эффект 37: serendipitous (Аттрактор Серрендипити)
// Описание: Фрактальный странный аттрактор в фазовом пространстве, создающий
//           неповторимые органические волновые переливы и переплетения.
// Параметры:
//   thisdelay (uint8_t) - скорость шага дискретного фазового уравнения
//   thishue   (uint8_t) - палитра фазовой траектории
// =============================================================================
void serendipitous() {
  uint16_t X = 0x012, Y = 0x015;
  while (check()) {
    fade(215);
    uint16_t Xn = X - (((uint32_t)Y * 85) >> 8);
    uint16_t Yn = Y + (X >> 1);
    X = Xn;
    Y = Yn;
    int pos = (X ^ Y) % SEG_LEN;
    uint8_t h = thishue + (triwave8((X >> 4) & 0xFF) >> 1);
    leds[pos] = leds[pos] + CHSV(h, 240, 255);
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 38: block_chase (Бегущие световые блоки)
// Описание: Группа синхронно перемещающихся цветных модульных сегментов
// (блоков)
//           с регулируемой плотностью, шагом модулей и реверсом движения.
// Параметры:
//   thisdelay (uint8_t) - скорость движения блоков
//   thishue   (uint8_t) - цвет первого блока
//   opt       (uint8_t) - битовая маска опций:
//                         OPT_DIR:  направление движения (вперед / назад)
//                         OPT_MODE: количество блоков (1 - три блока, 0 - два
//                         блока) бит 2:    ширина шага между блоками
// =============================================================================
void block_chase() {
  uint8_t step = 0;
  int8_t dir = (opt & OPT_DIR) ? 1 : -1;
  uint8_t count = (opt & OPT_MODE) ? 3 : 2;
  uint8_t shift = ((opt >> 2) & 1) + 1;
  uint8_t hue2 = thishue + 85;
  uint8_t hue3 = thishue + 170;
  while (check()) {
    step += dir;
    if (opt & OPT_EVEN)
      fade(200);
    for (int16_t i = 0; i < SEG_LEN; i++) {
      uint8_t m = ((i + step) >> shift) % count;
      leds[i] = (m == 0)
                    ? CHSV(thishue, 255, 255)
                    : (m == 1 ? CHSV(hue2, 255, 255) : CHSV(hue3, 255, 255));
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 72: warp_drive (Варп-прыжок в гиперпространство)
// Описание: Вход космического корабля в варп-скорость: неподвижные звезды
//           растягиваются в длинные световые лучи с ускорением от центра к
//           краям.
// Параметры:
//   thisdelay (uint8_t) - ускорение варп-поля
//   thishue   (uint8_t) - оттенок гиперпространственного излучения
// =============================================================================
void warp_drive() {
  uint16_t starPos[3] = {0};
  uint16_t starSpeed[3] = {0};
  int16_t half = SYM_SEG_LEN >> 1;
  while (check()) {
    fade(180);
    for (uint8_t s = 0; s < 3; s++) {
      int p = starPos[s] >> 8;
      if (p >= half) {
        starPos[s] = 0;
        starSpeed[s] = 60 + random8(60);
        p = 0;
      }
      starPos[s] += starSpeed[s];
      starSpeed[s] += (starSpeed[s] >> 3);
      TLED c = CHSV(thishue + s * 80, 160, 255);
      if (half - 1 - p >= 0)
        leds[half - 1 - p] = c;
      if (half + p < SYM_SEG_LEN)
        leds[half + p] = c;
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 39: vu_meter_sim (Аналоговый VU-Meter)
// Описание: Симуляция аналогового стрелочного индикатора уровня аудиосигнала
//           с динамическими пиками, инерционным баллистическим спадом и
//           зеленым/красным порогом.
// Параметры:
//   thisdelay (uint8_t) - баллистическая инерция спада стрелки
//   thishue   (uint8_t) - цвет базового диапазона громкости
// =============================================================================
void vu_meter_sim() {
  int16_t half = SEG_LEN >> 1;
  int16_t peakLeft = 0, peakRight = 0;
  uint8_t step = (half > 0) ? (80 / half) : 1;
  while (check()) {
    int16_t barLeft = random16(1, half + 1);
    int16_t barRight = random16(1, half + 1);
    if (barLeft > peakLeft)
      peakLeft = barLeft;
    else if (peakLeft > 0)
      peakLeft--;
    if (barRight > peakRight)
      peakRight = barRight;
    else if (peakRight > 0)
      peakRight--;
    uint8_t h = thishue;
    for (int16_t i = 0; i < half; i++) {
      h += step;
      leds[i] = (i < barLeft)
                    ? CHSV(h, 255, 255)
                    : ((i == peakLeft) ? TLED(255, 255, 255) : TLED(0, 0, 0));
      int rIdx = SEG_LEN - 1 - i;
      leds[rIdx] = (i < barRight) ? CHSV(h, 255, 255)
                                  : ((i == peakRight) ? TLED(255, 255, 255)
                                                      : TLED(0, 0, 0));
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 40: doppler_shift (Акустический эффект Доплера)
// Описание: Движущийся на околозвуковой скорости источник звука: сжатие длины
//           волны в синий спектр при приближении и растяжение в красный при
//           удалении.
// Параметры:
//   thisdelay (uint8_t) - скорость движения излучающего источника
//   thishue   (uint8_t) - собственная частота излучателя
// =============================================================================
void doppler_shift() {
  int pos = 0;
  int8_t dir = 1;
  uint8_t t = 0;
  while (check()) {
    t += 3;
    pos += dir * 2;
    if (pos >= SEG_LEN - 1 || pos <= 0) {
      dir = -dir;
      pos = constrain(pos, 0, SEG_LEN - 1);
    }
    for (int16_t i = 0; i < SEG_LEN; i++) {
      int d = i - pos;
      if ((dir > 0 && d > 0) || (dir < 0 && d < 0)) {
        uint8_t w = triwave8((abs(d) * 24 - t * 4) & 0xFF);
        leds[i] = CHSV(thishue + 140 + (w >> 3), 255, w);
      } else {
        uint8_t w = triwave8((abs(d) * 8 - t) & 0xFF);
        leds[i] = CHSV(thishue + (w >> 4), 255, w);
      }
    }
    leds[pos] = TLED(255, 255, 255);
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 41: prism_diffraction (Дисперсия света в призме Ньютона)
// Описание: Разложение пучка белого света стеклянной призмой в непрерывный
//           ньютоновский радужный спектр (красный $	o$ фиолетовый) со
//           сдвигом фокуса.
// Параметры:
//   thisdelay (uint8_t) - скорость прецессии оптической призмы
//   thishue   (uint8_t) - базовый спектральный угол
//   opt       (uint8_t) - битовая маска:
//                         OPT_HUE: градиентная дисперсия или монохромная
//                         дифракция
// =============================================================================
void prism_diffraction() {
  int16_t center = SEG_LEN >> 1;
  uint8_t angle = 0;
  uint8_t step = 255 / (center + 1);
  while (check()) {
    angle += 3;
    int16_t spread = scale8(triwave8(angle), center);
    fade(160);
    leds[center] = TLED(255, 255, 255);
    for (int16_t r = 1; r <= spread; r++) {
      TLED c =
          CHSV((opt & OPT_HUE) ? (thishue + r * step) : (r * step), 240, 255);
      if (center - r >= 0)
        leds[center - r] = c;
      if (center + r < SEG_LEN)
        leds[center + r] = c;
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 42: meissner_effect (Эффект Мейснера / Сверхпроводимость)
// Описание: Квантовая левитация постоянного магнита над сверхпроводником
//           за счет вытеснения магнитных силовых линий из объема
//           сверхпроводника.
// Параметры:
//   thisdelay (uint8_t) - период квадрупольных магнитных колебаний
//   thishue   (uint8_t) - цвет сверхпроводящего вихревого поля
// =============================================================================
void meissner_effect() {
  int16_t center = SEG_LEN >> 1;
  int16_t front = 0;
  while (check()) {
    if (++front > center) {
      front = 0;
      thishue += 35;
    }
    int16_t rem = center - front + 1;
    uint8_t mStep = (rem > 0) ? (155 / rem) : 155;
    for (int16_t i = 0; i < center; i++) {
      TLED c = (i < front) ? CHSV(130, 160, 220)
                           : CHSV(thishue, 255, 100 + (i - front) * mStep);
      if (center - 1 - i >= 0)
        leds[center - 1 - i] = c;
      if (center + i < SEG_LEN)
        leds[center + i] = c;
    }
    if (center - 1 - front >= 0)
      leds[center - 1 - front] = TLED(255, 255, 255);
    if (center + front < SEG_LEN)
      leds[center + front] = TLED(255, 255, 255);
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 43: quantum_spin_chain (Квантовая цепочка спинов Гейзенберга)
// Описание: Квантовая одномерная цепочка взаимодействующих спинов s=1/2:
//           распространение спиновых волн (магнонов) и коллективные перевороты
//           спина.
// Параметры:
//   thisdelay (uint8_t) - обменное взаимодействие J (скорость спиновой волны)
//   thishue   (uint8_t) - цвет антиферромагнитной решетки
// =============================================================================
void quantum_spin_chain() {
  int16_t spinonPos1 = 0;
  int16_t spinonPos2 = SEG_LEN - 1;
  while (check()) {
    spinonPos1 = (spinonPos1 + 1) % SEG_LEN;
    spinonPos2 = (spinonPos2 + SEG_LEN - 1) % SEG_LEN;
    for (int16_t i = 0; i < SEG_LEN; i++) {
      uint8_t isUp = (i & 1);
      TLED spinCol =
          isUp ? CHSV(thishue, 255, 160) : CHSV(thishue + 128, 255, 160);
      if ((spinonPos1 < spinonPos2 && (i >= spinonPos1 && i <= spinonPos2)) ||
          (spinonPos1 > spinonPos2 && (i >= spinonPos1 || i <= spinonPos2))) {
        spinCol =
            isUp ? CHSV(thishue + 128, 255, 160) : CHSV(thishue, 255, 160);
      }
      leds[i] = spinCol;
    }
    leds[spinonPos1] = TLED(255, 255, 255);
    leds[spinonPos2] = TLED(255, 255, 255);
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 44: snake_game (Автономная игра Змейка)
// Описание: Змейка ползет по одномерной ленте, ищет мерцающую еду (яблоко),
//           съедает его, увеличивается в длину и разворачивается при достижении
//           стенки.
// Параметры:
//   thisdelay (uint8_t) - скорость движения змейки
//   thishue   (uint8_t) - цвет туловища змейки (яблоко подсвечено белым)
// =============================================================================
void snake_game() {
  int16_t head = 5;
  int dir = 1;
  uint8_t len = 4;
  int16_t food = random16(6, SEG_LEN - 1);
  while (check()) {
    fade(120);
    head += dir;
    if (head == food) {
      if (len < 10)
        len++;
      food = random16(1, SEG_LEN - 1);
    }
    if (head <= 0 || head >= SEG_LEN - 1) {
      dir = -dir;
    }
    for (uint8_t s = 0; s < len; s++) {
      int bodyPos = head - (s * dir);
      if (bodyPos >= 0 && bodyPos < SEG_LEN) {
        uint8_t br = 255 - s * 24;
        leds[bodyPos] = (s == 0) ? TLED(255, 255, 255) : CHSV(thishue, 255, br);
      }
    }
    leds[food] = TLED(255, 0, 0);
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 73: newtons_cradle (Колыбель Ньютона)
// Описание: Упругая передача импульса и кинетической энергии через неподвижный
//           ряд металлических шаров: крайний шар отскакивает и возвращается.
// Параметры:
//   thisdelay (uint8_t) - период колебания подвешенных шаров
//   thishue   (uint8_t) - цвет полированных металлических шаров
// =============================================================================
void newtons_cradle() {
  int16_t center = SYM_SEG_LEN >> 1;
  uint8_t ballCount = 4;
  int16_t cStart = center - (ballCount >> 1);
  int16_t amp = center - 2;
  int8_t step = 0;
  int8_t side = -1;
  int8_t dir = 1;
  TLED ballColor = CHSV(thishue, 200, 220);
  while (check()) {
    fill(0, SYM_SEG_LEN, TLED(0, 0, 0));
    for (uint8_t k = 0; k < ballCount; k++)
      leds[cStart + k] = ballColor;

    int activeBall =
        (side < 0) ? (cStart - step) : (cStart + ballCount - 1 + step);
    if (activeBall >= 0 && activeBall < SYM_SEG_LEN)
      leds[activeBall] = (step == 0) ? TLED(255, 255, 255) : ballColor;

    step += dir;
    if (step >= amp) {
      dir = -1;
    } else if (step <= 0) {
      step = 0;
      dir = 1;
      side = -side;
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 45: sinelon (Гармонический бегущий огонек Sinelon)
// Описание: Канонический алгоритм FastLED: гармонический синусоидальный огонек
//           плавно качается вперед и назад, оставляя мягкий затухающий шлейф.
// Параметры:
//   thisdelay (uint8_t) - скорость маятникового качания
//   thishue   (uint8_t) - плавно меняющийся оттенок огонька
// =============================================================================
void sinelon() {
  uint8_t beat = 0;
  while (check()) {
    fade(192);
    beat += 3;
    int16_t pos = (((uint32_t)triwave8(beat) * (SEG_LEN - 1)) >> 8);
    leds[pos] = leds[pos] + CHSV(thishue++, 255, 255);
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 74: meteor_rain (Метеорный дождь)
// Описание: Падение яркого метеора с белым ядром и искрящимся шлейфом
//           с динамическим случайным угасанием хвоста (Meteor Rain).
// Параметры:
//   thisdelay (uint8_t) - скорость падения метеоров
//   thishue   (uint8_t) - цвет хвоста метеора
// =============================================================================
void meteor_rain() {
  uint8_t meteorSize = 8;
  int16_t pos = 0;
  while (check()) {
    fade(175);
    pos += 2;
    if (pos >= SYM_SEG_LEN + meteorSize) {
      pos = 0;
      thishue += 35;
    }
    for (uint8_t j = 0; j < meteorSize; j++) {
      int16_t p = pos - j;
      if (p >= 0 && p < SYM_SEG_LEN) {
        leds[p] =
            (j == 0) ? TLED(255, 255, 255) : CHSV(thishue, 240, 255 - j * 28);
      }
    }
    if (random8(10) < 4) {
      int16_t spark = pos - random8(meteorSize + 4);
      if (spark >= 0 && spark < SYM_SEG_LEN)
        leds[spark] = TLED(255, 255, 255);
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 46: juggle (Жонглирование огоньками Juggle)
// Описание: Классический алгоритм FastLED: 6 разноцветных световых шариков
//           летают по ленте на различных гармонических некратных частотах.
// Параметры:
//   thisdelay (uint8_t) - темп движения жонглирующих шариков
//   thishue   (uint8_t) - цветовой спектр шариков
// =============================================================================
void juggle() {
  uint8_t phase[6] = {0, 42, 85, 128, 170, 213};
  uint8_t speed[6] = {3, 5, 7, 9, 11, 13};
  while (check()) {
    fade(192);
    for (uint8_t i = 0; i < 6; i++) {
      phase[i] += speed[i];
      int16_t p = (((uint32_t)triwave8(phase[i]) * (SEG_LEN - 1)) >> 8);
      leds[p] = leds[p] + CHSV(thishue + i * 42, 220, 255);
    }
    thishue++;
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 47: bpm (Музыкальные биения BPM)
// Описание: Ритмичные цветные импульсные волны, пульсирующие в такт заданной
//           музыкальной частоте ударов в минуту (Beats Per Minute) от FastLED.
// Параметры:
//   thisdelay (uint8_t) - темп биений (BPM)
//   thishue   (uint8_t) - базовая палитра пульсирующих волн
// =============================================================================
void bpm() {
  uint8_t beat = 0;
  while (check()) {
    beat += 4;
    uint8_t bVal = triwave8(beat);
    for (int16_t i = 0; i < SEG_LEN; i++) {
      uint8_t bHue = thishue + (i << 2);
      uint8_t br = triwave8(bVal + (i << 3));
      leds[i] = CHSV(bHue, 240, br);
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 48: lightning (Реалистичная грозовая молния)
// Описание: Алгоритм грозы от Эндрю Тулина: серии робких пре-вспышек в грозовом
//           облаке завершаются ослепительным ударом молнии и раскатами грома.
// Параметры:
//   thisdelay (uint8_t) - интервалы между разрядами грозы
//   thishue   (uint8_t) - оттенок грозового неба и разряда
// =============================================================================
void lightning() {
  while (check()) {
    fade(140);
    showseg_delay(thisdelay);
    if (!check())
      return;

    int16_t strikePos = random16(10, SEG_LEN - 10);
    uint8_t flashes = random8(4, 8);
    for (uint8_t f = 0; f < flashes; f++) {
      uint8_t flashLen = random8(8, 25);
      TLED strikeColor =
          (f & 1) ? TLED(255, 255, 255) : CHSV(thishue + 140, 160, 255);
      int16_t start = constrain(strikePos - (flashLen >> 1), 0, SEG_LEN - 1);
      int16_t len = min((int16_t)flashLen, (int16_t)(SEG_LEN - start));
      fill(start, len, strikeColor);
      showseg_delay(random8(10, 30));
      fill(start, len, TLED(0, 0, 0));
      showseg_delay(random8(20, 60));
      if (!check())
        return;
    }
  }
}
// =============================================================================
// Эффект 49: water_ripple (Круги на воде от капель дождя)
// Описание: Алгоритм WLED: падающие капли дождя создают на водной глади
//           расходящиеся затухающие волны, интерферирующие друг с другом.
// Параметры:
//   thisdelay (uint8_t) - скорость распространения волн по воде
//   thishue   (uint8_t) - цвет водной поверхности
// =============================================================================
void water_ripple() {
  int16_t center[3] = {-1, -1, -1};
  int16_t step[3] = {0, 0, 0};
  int16_t maxRadius = min((int16_t)30, (int16_t)(SEG_LEN >> 1));
  if (maxRadius < 5)
    maxRadius = 5;
  while (check()) {
    fade(180);
    for (uint8_t d = 0; d < 3; d++) {
      if (center[d] < 0 && random8(15) == 0) {
        center[d] = random16(5, SEG_LEN - 5);
        step[d] = 0;
      }
      if (center[d] >= 0) {
        step[d]++;
        uint8_t br = 255 - ((uint16_t)step[d] * 255 / maxRadius);
        TLED c = CHSV(thishue + d * 25, 200, br);
        int16_t left = center[d] - step[d];
        int16_t right = center[d] + step[d];
        if (left >= 0 && left < SEG_LEN)
          leds[left] = leds[left] + c;
        if (right >= 0 && right < SEG_LEN)
          leds[right] = leds[right] + c;
        if (step[d] >= maxRadius)
          center[d] = -1;
      }
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 50: fireflies (Рой светлячков в лесу)
// Описание: Рой биолюминесцентных светлячков в ночной чаще: каждый светлячок
//           плавно и независимо «дышит» светом, медленно перемещаясь по ленте.
// Параметры:
//   thisdelay (uint8_t) - темп дыхания и перемещения светлячков
//   thishue   (uint8_t) - биолюминесцентный оттенок свечения
// =============================================================================
void fireflies() {
  int16_t pos[6];
  int8_t speed[6];
  uint8_t phase[6];
  for (uint8_t i = 0; i < 6; i++) {
    pos[i] = random16(0, LED_COUNT);
    speed[i] = (random8(2) ? 1 : -1) * (int8_t)random8(1, 3);
    phase[i] = random8();
  }
  while (check()) {
    fade(170);
    for (uint8_t i = 0; i < 6; i++) {
      pos[i] += speed[i];
      if (pos[i] <= 0 || pos[i] >= LED_COUNT - 1)
        speed[i] = -speed[i];
      phase[i] += 5;
      uint8_t glow = triwave8(phase[i]);
      if (glow > 100) {
        uint8_t diff = glow - 100;
        uint8_t v = diff + (diff >> 4);
        TLED c = CHSV(thishue + 45 + (i << 2), 240, v);
        leds[pos[i]] = leds[pos[i]] + c;
        TLED cHalf(c.get_r() >> 1, c.get_g() >> 1, c.get_b() >> 1);
        if (pos[i] > 0)
          leds[pos[i] - 1] = leds[pos[i] - 1] + cHalf;
        if (pos[i] < LED_COUNT - 1)
          leds[pos[i] + 1] = leds[pos[i] + 1] + cHalf;
      }
    }
    show_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 75: dna_helix (Двойная спираль ДНК)
// Описание: 3D-проекция пространственного вращения двойной спирали ДНК
//           с периодическими соединительными перемычками азотистых оснований.
// Параметры:
//   thisdelay (uint8_t) - скорость вращения спирали в пространстве
//   thishue   (uint8_t) - спектральный цвет молекулярных нитей
// =============================================================================
void dna_helix() {
  uint8_t t = 0;
  while (check()) {
    t += 3;
    fade(160);
    for (int16_t i = 0; i < SYM_SEG_LEN; i++) {
      uint8_t phase1 = (i * 14 + t) & 0xFF;
      uint8_t phase2 = (i * 14 + t + 128) & 0xFF;
      uint8_t s1 = triwave8(phase1);
      uint8_t s2 = triwave8(phase2);
      if (s1 > 220) {
        leds[i] = leds[i] + CHSV(thishue, 255, s1);
      }
      if (s2 > 220) {
        leds[i] = leds[i] + CHSV(thishue + 128, 255, s2);
      }
      if (abs_diff8(s1, s2) < 12 && s1 > 100) {
        leds[i] = blend(leds[i], TLED(255, 255, 255), 180);
      }
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 76: supernova (Вспышка сверхновой звезды)
// Описание: Астрофизическая модель: аккреция и гравитационный коллапс звезды
//           переходят в колоссальный термоядерный взрыв и расширение
//           туманности.
// Параметры:
//   thisdelay (uint8_t) - скорость фаз взрыва сверхновой
//   thishue   (uint8_t) - температура спектра остатка сверхновой
// =============================================================================
void supernova() {
  int16_t core = random16(20, SYM_SEG_LEN - 20);
  uint8_t state = 0; // 0=accretion, 1=flash, 2=nebula expanding
  uint8_t timer = 0;
  int16_t radius = 0;
  while (check()) {
    if (state == 0) { // Accretion
      fade(175);
      timer += 4;
      uint8_t pulse = triwave8(timer);
      leds[core] = CHSV(thishue + 30, 180, pulse);
      int16_t offset = 15 - (timer >> 4);
      int16_t in1 = core - offset;
      int16_t in2 = core + offset;
      TLED inColor = CHSV(thishue + 160, 240, 150);
      if (in1 >= 0 && in1 < SYM_SEG_LEN)
        leds[in1] = inColor;
      if (in2 >= 0 && in2 < SYM_SEG_LEN)
        leds[in2] = inColor;
      if (timer > 230) {
        state = 1;
      }
    } else if (state == 1) { // Supernova explosion flash
      fill(0, SYM_SEG_LEN, TLED(255, 255, 255));
      showseg_delay(30);
      if (!check())
        return;
      state = 2;
      radius = 0;
    } else { // Expanding remnant nebula
      fade(170);
      radius += 2;
      int16_t l = core - radius;
      int16_t r = core + radius;
      uint8_t nebHue = thishue + radius * 3;
      if (l >= 0 && l < SYM_SEG_LEN)
        leds[l] = CHSV(nebHue, 220, 255);
      if (r >= 0 && r < SYM_SEG_LEN)
        leds[r] = CHSV(nebHue + 30, 220, 255);
      if (radius > (SYM_SEG_LEN >> 2)) {
        state = 0;
        timer = 0;
        core = random16(20, SYM_SEG_LEN - 20);
        thishue += 50;
      }
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 51: neon_heartbeat (Неоновый кардиограф ЭКГ)
// Описание: Медицинский кардиограф: прохождение электрокардиограммы P-Q-R-S-T
//           по неоновому экрану с ослепительным зубцом R и люминесцентным
//           следом.
// Параметры:
//   thisdelay (uint8_t) - частота сердечных сокращений (пульс)
//   thishue   (uint8_t) - неоновый цвет осциллографа
// =============================================================================
void neon_heartbeat() {
  int16_t head = 0;
  uint8_t beatPhase = 0;
  while (check()) {
    fade(180);
    head += 2;
    if (head >= SEG_LEN) {
      head = 0;
      thishue += 32;
    }
    beatPhase = ((uint32_t)head * 255) / SEG_LEN;
    uint8_t amp = 0;
    if (beatPhase >= 35 && beatPhase <= 55)
      amp = triwave8((beatPhase - 35) * 12);
    else if (beatPhase >= 95 && beatPhase <= 100)
      amp = (100 - beatPhase) * 40;
    else if (beatPhase > 100 && beatPhase <= 110)
      amp = 255;
    else if (beatPhase > 110 && beatPhase <= 118)
      amp = 0;
    else if (beatPhase >= 150 && beatPhase <= 185)
      amp = triwave8((beatPhase - 150) * 7);

    if (amp > 0) {
      TLED c = (amp == 255) ? TLED(255, 255, 255) : CHSV(thishue, 220, amp);
      leds[head] = c;
      if (head > 0)
        leds[head - 1] = leds[head - 1] +
                         (TLED(c.get_r() >> 1, c.get_g() >> 1, c.get_b() >> 1));
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 54: pulsar_beacon (Релятивистский пульсар)
// Описание: Быстровращающаяся нейтронная звезда: узкие релятивистские джеты
//           синхротронного излучения периодически освещают ленту мощным маяком.
// Параметры:
//   thisdelay (uint8_t) - период вращения нейтронной звезды
//   thishue   (uint8_t) - спектральный цвет магнитных полюсов
// =============================================================================
void pulsar_beacon() {
  uint8_t angle = 0;
  int16_t center = SEG_LEN >> 1;
  while (check()) {
    angle += 7;
    fade(160);
    leds[center] = TLED(255, 255, 255);
    uint8_t beam = triwave8(angle);
    if (beam > 200) {
      uint8_t bdiff = beam - 200;
      uint8_t br = (bdiff << 2) + (bdiff >> 1);
      TLED c = CHSV(thishue + 160, 200, br);
      int16_t reach = scale8(br, center);
      for (int16_t r = 1; r <= reach; r++) {
        if (center - r >= 0)
          leds[center - r] = leds[center - r] + c;
        if (center + r < SEG_LEN)
          leds[center + r] = leds[center + r] + c;
      }
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 52: synthwave_grid (Неоновая сетка Synthwave / Outrun)
// Описание: Атмосфера 80-х: бегущая неоновая сетка киберпанк-автострады
//           с квадратичной перспективой удаляющегося к горизонту заката.
// Параметры:
//   thisdelay (uint8_t) - скорость движения по виртуальной сетке
//   thishue   (uint8_t) - неоновая палитра (маджента / циан)
// =============================================================================
void synthwave_grid() {
  uint8_t offset = 0;
  int16_t center = SEG_LEN >> 1;
  while (check()) {
    offset += 3;
    for (int16_t i = 0; i < center; i++) {
      uint8_t persp = ((uint16_t)i * i) / (center ? center : 1);
      uint8_t wave = triwave8(persp * 8 - offset);
      TLED c = (wave > 210) ? CHSV(215, 240, 255)
                            : CHSV(thishue + (i << 2), 255, wave >> 2);
      if (center - 1 - i >= 0)
        leds[center - 1 - i] = c;
      if (center + i < SEG_LEN)
        leds[center + i] = c;
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 53: geiger_counter (Счетчик Гейгера / Камера Вильсона)
// Описание: Детектор ионизирующих излучений: случайные высокоэнергетические
//           космические частицы прошивают камеру Вильсона, оставляя треки
//           конденсации.
// Параметры:
//   thisdelay (uint8_t) - интенсивность радиационного фона
//   thishue   (uint8_t) - цвет ионизационных треков
// =============================================================================
void geiger_counter() {
  while (check()) {
    fade(175);
    if (random8(10) < 4) {
      int16_t trackStart = random16(0, LED_COUNT);
      int8_t dir = (random8(2) == 0) ? 1 : -1;
      uint8_t trackLen = random8(10, 35);
      TLED ionColor = (random8(4) == 0) ? TLED(255, 255, 255)
                                        : CHSV(thishue + random8(32), 220, 255);
      for (uint8_t step = 0; step < trackLen; step++) {
        int16_t p = trackStart + step * dir;
        if (p >= 0 && p < LED_COUNT) {
          leds[p] = ionColor;
        }
      }
    }
    show_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 55: beatwave (4-волновой интерференционный ландшафт)
// Описание: Классический алгоритм FastLED: суперпозиция 4 гармонических волн
//           с взаимными биениями фаз, создающая бесконечные волновые переливы.
// Параметры:
//   thisdelay (uint8_t) - скорость наложения 4 волновых фронтов
//   thishue   (uint8_t) - базовая палитра интерференционного ландшафта
// =============================================================================
void beatwave() {
  uint8_t t1 = 0, t2 = 0, t3 = 0, t4 = 0;
  while (check()) {
    t1 += 2;
    t2 += 3;
    t3 += 5;
    t4 += 7;
    for (int16_t i = 0; i < SEG_LEN; i++) {
      uint8_t b1 = triwave8((i * 6 + t1) & 0xFF);
      uint8_t b2 = triwave8((i * 10 - t2) & 0xFF);
      uint8_t b3 = triwave8((i * 14 + t3) & 0xFF);
      uint8_t b4 = triwave8((i * 18 - t4) & 0xFF);
      uint8_t waveSum = ((uint16_t)b1 + b2 + b3 + b4) >> 2;
      leds[i] = CHSV(thishue + (waveSum >> 1), 240, waveSum);
    }
    showseg_delay(thisdelay);
  }
}

static inline uint8_t noise_hash(uint16_t x) {
  x = (x * 2053u) + 13849u;
  return (uint8_t)(x ^ (x >> 8));
}

static inline uint8_t inoise1d(uint16_t x) {
  uint16_t x0 = x >> 8;
  uint8_t f = x & 0xFF;
  uint8_t u = triwave8(f >> 1);
  uint8_t h0 = noise_hash(x0);
  uint8_t h1 = noise_hash(x0 + 1);
  return (h0 < h1) ? (h0 + scale8(h1 - h0, u)) : (h0 - scale8(h0 - h1, u));
}
// =============================================================================
// Эффект 77: liquid_silk (Жидкий шелк / Живая туманность)
// Описание: 1D фрактальный броуновский шум (fBm Perlin-подобный шум на
// целочисленном
//           хеше с эрмитовой интерполяцией): живая, струящаяся органическая
//           текстура.
// Параметры:
//   thisdelay (uint8_t) - скорость течения складок шелка
//   thishue   (uint8_t) - спектральный оттенок шелковой ткани
// =============================================================================
void liquid_silk() {
  uint16_t t1 = 0, t2 = 0;
  while (check()) {
    t1 += 7;
    t2 += 13;
    for (int16_t i = 0; i < SYM_SEG_LEN; i++) {
      uint16_t coord1 = (uint16_t)i * 21 + t1;
      uint16_t coord2 = (uint16_t)i * 42 - t2;
      uint8_t n1 = inoise1d(coord1);
      uint8_t n2 = inoise1d(coord2);
      uint8_t fbm = (n1 >> 1) + (n2 >> 2);
      uint8_t hue = thishue + fbm;
      uint8_t br = triwave8(fbm + (i << 1));
      leds[i] = CHSV(hue, 230, br);
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 78: stroboscopic_levitation (Стробоскопическая левитация капель)
// Описание: Оптическая иллюзия биений частоты капель и стробоскопа: падающие
// капли
//           воды плавно замедляются, зависают в воздухе и левитируют вверх
//           против g.
// Параметры:
//   thisdelay (uint8_t) - частота биений стробоскопического строба
//   thishue   (uint8_t) - цвет капель и отраженного света
// =============================================================================
void stroboscopic_levitation() {
  uint8_t dropPhase = 0;
  uint8_t strobeFreq = 0;
  int16_t spacing = SYM_SEG_LEN / 6;
  if (spacing < 4)
    spacing = 4;
  while (check()) {
    fade(160);
    dropPhase += 4;
    strobeFreq += 1;
    int8_t beatSlip = (int8_t)triwave8(strobeFreq) - 128;
    int16_t shift = (dropPhase + ((beatSlip * dropPhase) >> 7));
    for (int16_t base = 0; base < SYM_SEG_LEN + spacing; base += spacing) {
      int16_t p = (base + shift) % SYM_SEG_LEN;
      if (p >= 0 && p < SYM_SEG_LEN) {
        leds[p] = TLED(255, 255, 255);
        if (p > 0)
          leds[p - 1] = CHSV(thishue + 140, 220, 180);
        if (p < SYM_SEG_LEN - 1)
          leds[p + 1] = CHSV(thishue + 140, 220, 180);
      }
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 79: quantum_tunneling (Квантовое туннелирование)
// Описание: Волновой пакет летит к потенциальному барьеру: стоячая
// интерференционная
//           волна на отражении и затухающая волна де Бройля, туннелирующая
//           сквозь барьер.
// Параметры:
//   thisdelay (uint8_t) - скорость волнового пакета
//   thishue   (uint8_t) - квантовый цвет фазового состояния
// =============================================================================
void quantum_tunneling() {
  int16_t barrier = SYM_SEG_LEN >> 1;
  int16_t packetPos = 0;
  int8_t speed = 2;
  uint8_t t = 0;
  uint8_t decay = 120;
  while (check()) {
    t += 3;
    fade(165);
    leds[barrier] = TLED(255, 255, 255);
    if (barrier > 0)
      leds[barrier - 1] = CHSV(180, 255, 80);
    if (barrier < SYM_SEG_LEN - 1)
      leds[barrier + 1] = CHSV(180, 255, 80);

    packetPos += speed;
    if (packetPos >= barrier - 2) {
      for (int16_t i = 0; i < barrier; i++) {
        int16_t d = barrier - i;
        uint8_t psi = triwave8((d * 18 - t * 4) & 0xFF);
        if (psi > 160) {
          leds[i] = leds[i] + CHSV(thishue, 240, (psi - 160) * 2);
        }
      }
      for (int16_t i = barrier + 1; i < SYM_SEG_LEN; i++) {
        int16_t dist = i - barrier;
        uint8_t amp = (dist < 20) ? qsub8(decay, dist * 6) : 0;
        if (amp > 0) {
          uint8_t deBroglie = triwave8((dist * 12 - t * 2) & 0xFF);
          leds[i] = leds[i] + CHSV(thishue + 130, 220, scale8(amp, deBroglie));
        }
      }
      if (++decay > 200) {
        packetPos = 0;
        decay = 120;
        thishue += 45;
      }
    } else {
      for (int8_t k = -4; k <= 4; k++) {
        int16_t p = packetPos + k;
        if (p >= 0 && p < barrier) {
          uint8_t amp = 255 - abs(k) * 55;
          uint8_t phase = triwave8((k * 30 + t * 6) & 0xFF);
          leds[p] = CHSV(thishue, 240, scale8(amp, phase));
        }
      }
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 56: holographic_opal (Голографический опал / Тонкопленочная
// интерференция) Описание: Оптическая интерференция света в тонких пленках
// (мыльные пузыри / опал):
//           трехволновая интерференция (R, G, B) порождает радужные переливы с
//           белыми бликами.
// Параметры:
//   thisdelay (uint8_t) - скорость оптической модуляции толщины пленки
//   thishue   (uint8_t) - спектральный хроматический сдвиг
// =============================================================================
void holographic_opal() {
  uint8_t phaseR = 0, phaseG = 0, phaseB = 0;
  while (check()) {
    phaseR += 2;
    phaseG += 3;
    phaseB += 5;
    for (int16_t i = 0; i < SEG_LEN; i++) {
      uint8_t film = triwave8((i * 7 + phaseR) & 0xFF);
      uint8_t r = triwave8((film * 3 + phaseR) & 0xFF);
      uint8_t g = triwave8((film * 4 + phaseG + thishue) & 0xFF);
      uint8_t b = triwave8((film * 5 + phaseB) & 0xFF);
      if (r > 240 && g > 240 && b > 240) {
        leds[i] = TLED(255, 255, 255);
      } else {
        leds[i] = TLED(r, g, b);
      }
    }
    showseg_delay(thisdelay);
  }
}
// =============================================================================
// Эффект 57: deep_sea_jellyfish (Глубоководная светящаяся сифонофора)
// Описание: Биолюминесцентная медуза с ритмично пульсирующим полупрозрачным
// куполом
//           при реактивном движении и длинным струящимся шлейфом мерцающих
//           щупалец.
// Параметры:
//   thisdelay (uint8_t) - частота реактивных пульсаций купола
//   thishue   (uint8_t) - оттенок биолюминесценции (глубоководный циан /
//   ультрамарин)
// ==============================================================================
void deep_sea_jellyfish() {
  int16_t bellPos = 0;
  int8_t dir = 1;
  uint8_t pulsePhase = 0;
  while (check()) {
    fade(175);
    pulsePhase += 6;
    uint8_t bellExpansion = triwave8(pulsePhase);
    int8_t step = (bellExpansion > 160) ? 3 : 1;
    bellPos += dir * step;
    if (bellPos >= SEG_LEN - 1 || bellPos <= 0) {
      dir = -dir;
      bellPos = constrain(bellPos, 0, SEG_LEN - 1);
      thishue += 40;
    }
    uint8_t bellRadius = (bellExpansion >> 6) + 1;
    for (int8_t r = -bellRadius; r <= bellRadius; r++) {
      int16_t p = bellPos + r;
      if (p >= 0 && p < SEG_LEN) {
        uint8_t br = 255 - abs(r) * 60;
        leds[p] = (r == 0) ? TLED(255, 255, 255) : CHSV(thishue + 130, 200, br);
      }
    }
    for (uint8_t t = 1; t <= 5; t++) {
      int16_t tentacle = bellPos - (t * 3 * dir);
      if (tentacle >= 0 && tentacle < SEG_LEN) {
        uint8_t tentacleGlow = triwave8((pulsePhase - t * 40) & 0xFF);
        leds[tentacle] = leds[tentacle] +
                         CHSV(thishue + 150 + t * 10, 255, tentacleGlow >> 1);
      }
    }
    showseg_delay(thisdelay);
  }
}

// =============================================================================
// СПИСОК ЭФФЕКТОВ
// Единственный источник истины: индекс = номер эффекта в UART-протоколе
// ("E:NN"). Из списков генерируются switch-диспетчер и TOTAL_EFFECTS.
// =============================================================================
#define EFFECT_LIST_OUTER(X)                                                       \
  X(0, rainbow_cycle)                     /* радужный цикл */ \
  X(1, dendritic_crystal)                 /* рост кристаллов */ \
  X(2, heat_diffusion)                    /* тепловая диффузия */ \
  X(3, quantum_collapse)                  /* квантовый коллапс */ \
  X(4, plasma_ball)                       /* плазменный шар */ \
  X(5, shift_stream)                      /* сдвиговый поток */ \
  X(6, radiation)                         /* пульс радиации/сердца */ \
  X(7, optical_tweezers)                  /* оптический пинцет */ \
  X(8, colorWipe)                         /* заливка/стирание */ \
  X(9, random_fill)                       /* случайное заполнение */ \
  X(10, theater_chase)                    /* театральный бегущий огонь */ \
  X(11, pendulum_wave)                    /* маятниковая волна */ \
  X(12, strobe)                           /* стробоскоп */ \
  X(13, police_flasher)                   /* спецмигалка */ \
  X(14, morse_code)                       /* азбука Морзе */ \
  X(15, flicker_lamp)                     /* неисправная лампа */ \
  X(16, sparkles)                         /* искры */ \
  X(17, resonant_wave)                    /* резонансные волны */ \
  X(18, acoustic_resonance)               /* акустический резонанс */ \
  X(19, wandering_lights)                 /* блуждающие частицы */ \
  X(20, hydraulic_jump)                   /* гидравлический скачок */ \
  X(21, solar_wind)                       /* солнечный ветер */ \
  X(22, gravitational_lensing)            /* гравитационное линзирование */ \
  X(23, flame_ambience)                   /* уютный очаг */ \
  X(24, wandering_tongue)                 /* язык пламени */ \
  X(25, quantum_chaos)                    /* квантовый хаос */ \
  X(26, bubble_sort)                      /* пузырьковая сортировка */ \
  X(27, rule30_ca)                        /* клеточный автомат Rule 30 */ \
  X(28, kaleidoscope)                     /* калейдоскоп */ \
  X(29, pacman_run)                       /* Пакман */ \
  X(30, pride2015)                        /* палитра Pride 2015 */ \
  X(31, blendwave)                        /* смешивание палитр */ \
  X(32, twinkle_fox)                      /* мерцание TwinkleFox */ \
  X(33, seismic_waves)                    /* сейсмические волны */ \
  X(34, sonoluminescence)                 /* сонолюминесценция */ \
  X(35, magnetic_reconnection)            /* магнитное пересоединение */ \
  X(36, percolation_breakdown)            /* перколяционный пробой */ \
  X(37, serendipitous)                    /* аттрактор Серендипити */ \
  X(38, block_chase)                      /* бегущие блоки */ \
  X(39, vu_meter_sim)                     /* VU-метр */ \
  X(40, doppler_shift)                    /* эффект Доплера */ \
  X(41, prism_diffraction)                /* дисперсия в призме */ \
  X(42, meissner_effect)                  /* эффект Мейснера */ \
  X(43, quantum_spin_chain)               /* спиновая цепочка */ \
  X(44, snake_game)                       /* змейка */ \
  X(45, sinelon)                          /* Sinelon */ \
  X(46, juggle)                           /* жонглирование */ \
  X(47, bpm)                              /* BPM-биения */ \
  X(48, lightning)                        /* молния */ \
  X(49, water_ripple)                     /* круги на воде */ \
  X(50, fireflies)                        /* светлячки */ \
  X(51, neon_heartbeat)                   /* неоновый кардиограф */ \
  X(52, synthwave_grid)                   /* Synthwave-сетка */ \
  X(53, geiger_counter)                   /* счётчик Гейгера */ \
  X(54, pulsar_beacon)                    /* пульсар */ \
  X(55, beatwave)                         /* биения четырёх волн */ \
  X(56, holographic_opal)                 /* голографический опал */ \
  X(57, deep_sea_jellyfish)               /* глубоководная медуза */

#define EFFECT_LIST_INNER(X)                                                       \
  X(58, cellular_fire)                    /* клеточный автомат огня */ \
  X(59, nuclear_fission)                  /* деление ядра */ \
  X(60, kdv_solitons)                     /* солитоны КдВ */ \
  X(61, Fire)                             /* канонический Fire2012 */ \
  X(62, BouncingBalls)                    /* прыгающие мячи */ \
  X(63, fireworks)                        /* салют */ \
  X(64, comet)                            /* кометы */ \
  X(65, boba1)                            /* 1D-тетрис */ \
  X(66, barcode_scanner)                  /* сканер штрих-кода */ \
  X(67, cylon_bounce)                     /* глаз Сайлона */ \
  X(68, wave_synthesis)                   /* синтез волн/палитр */ \
  X(69, gravitational_fall)               /* гравитационное падение */ \
  X(70, halloween_eyes)                   /* глаза в темноте */ \
  X(71, shockwave)                        /* ударная волна */ \
  X(72, warp_drive)                       /* варп-прыжок */ \
  X(73, newtons_cradle)                   /* колыбель Ньютона */ \
  X(74, meteor_rain)                      /* метеорный дождь */ \
  X(75, dna_helix)                        /* спираль ДНК */ \
  X(76, supernova)                        /* сверхновая */ \
  X(77, liquid_silk)                      /* жидкий шёлк */ \
  X(78, stroboscopic_levitation)          /* стробо-левитация */ \
  X(79, quantum_tunneling)                /* квантовое туннелирование */

#define EFFECT_SWITCH_CASE(i, name)                                          \
  case i:                                                                    \
    name();                                                                  \
    break;
#define EFFECT_COUNT_ITEM(i, name) +1
enum {
  TOTAL_EFFECTS = 0
  EFFECT_LIST_OUTER(EFFECT_COUNT_ITEM)
  EFFECT_LIST_INNER(EFFECT_COUNT_ITEM)
};
#undef EFFECT_COUNT_ITEM

// Эффекты с этого индекса используют фиксированную геометрию SYM_SEG_*.
#define SYM_EFFECT_START 58

static void uart_init() {
  UBRR0H = 0;
  UBRR0L = 16;           // 115200 baud при 16MHz (U2X0 = 1)
  UCSR0A = (1 << U2X0);
  UCSR0B = (1 << TXEN0); // Включаем только передатчик TX, без прерываний и RX-буфера
}

static void uart_write(char c) {
  while (!(UCSR0A & (1 << UDRE0)))
    ;
  UDR0 = c;
}

static void uart_print_effect(uint8_t eff) {
  uart_write('E');
  uart_write(':');
  uint8_t tens = 0;
  while (eff >= 10) {
    eff -= 10;
    tens++;
  }
  uart_write('0' + tens);
  uart_write('0' + eff);
  uart_write('\r');
  uart_write('\n');
}

void setup() {
  wdt_enable(WDTO_4S); // Аппаратный сторожевой таймер на 4 секунды (защита от
                       // зависаний)
  uart_init(); // открыть порт для связи (высокая скорость 115200)

#ifdef PHOTO_GND_PIN
  pinMode(PHOTO_GND_PIN, OUTPUT);
  digitalWrite(PHOTO_GND_PIN, LOW); // формируем землю (0V) на пине датчика
#endif
#ifdef PHOTO_VCC_PIN
  pinMode(PHOTO_VCC_PIN, OUTPUT);
  digitalWrite(PHOTO_VCC_PIN, HIGH); // формируем питание (+5V) на пине датчика
#endif
#if PHOTO_INTERNAL_PULLUP
  pinMode(PHOTO_SENSOR, INPUT_PULLUP); // подтяжка для 2-пинового фоторезистора
#endif

#ifndef ADAPT_LIGHT
  LEDS.setBrightness(MAX_BRIGHT); // без фотосенсора не оставляем ленту чёрной
#endif

  random16_set_seed(analogRead(0));
  check(); // первичное измерение яркости (при ADAPT_LIGHT)
}

// =============================================================================
// ПЕРЕХОДЫ МЕЖДУ ЭФФЕКТАМИ
// Общий набор «затухающих» анимаций. Режим 7 — прямой стык без перехода.
// =============================================================================
static void show_transition() {
  uint8_t mode = random8(0, 8);
  if (mode == 7)
    return;

  if (mode == 0) { // мягкое экспоненциальное затухание в темноту
    for (uint8_t i = 0; i < 28; i++) {
      fade(210);
      show_delay(18);
    }
    return;
  }

  if (mode == 4) { // искрящееся рассеивание
    for (uint8_t s = 0; s < 25; s++) {
      for (uint8_t k = 0; k < 25; k++)
        leds[random16(LED_COUNT)] = 0;
      fade(215);
      show_delay(18);
    }
    return;
  }

  if (mode == 5 || mode == 6) { // сдвиговая шторка вправо/влево
    int16_t i = (mode == 5) ? 0 : LED_COUNT - 5;
    while (i >= 0 && i < LED_COUNT) {
      fill(i, 5, TLED(0, 0, 0));
      fade(210);
      show_delay(14);
      i += (mode == 5) ? 5 : -5;
    }
    return;
  }

  // Режимы 1..3: закрываем ленту блоками (к центру, от центра или в одну
  // сторону).
  uint8_t steps = (mode == 3) ? 30 : 28;
  uint16_t stepSize = (mode == 3) ? 6 : 3;
  uint16_t limit = (mode == 3) ? LED_COUNT : LED_COUNT / 2;
  uint8_t right = (mode == 3) ? random8(0, 2) : 0;
  for (uint8_t s = 0; s < steps; s++) {
    uint16_t startIdx = s * stepSize;
    uint16_t endIdx = startIdx + stepSize;
    if (endIdx > limit)
      endIdx = limit;
    for (uint16_t i = startIdx; i < endIdx; i++) {
      if (mode == 1) {
        leds[i] = 0;
        leds[LED_COUNT - 1 - i] = 0;
      } else if (mode == 2) {
        leds[LED_COUNT / 2 - i] = 0;
        leds[LED_COUNT / 2 + i] = 0;
      } else if (right) {
        leds[i] = 0;
      } else {
        leds[LED_COUNT - 1 - i] = 0;
      }
    }
    fade((mode == 3) ? 240 : 235);
    show_delay((mode == 3) ? 16 : 18);
  }
}

void loop() {
  wdt_reset();
  // Длительность показа эффекта: от ~20 до ~50 секунд (полное раскрытие физики)
  TIME2EXIT = (unsigned long)random8(80, 200) * 256u;
  TIME2EXIT += NOW;

  // Случайный выбор одного из TOTAL_EFFECTS (80) уникальных эффектов
  uint8_t effect = random8(0, (uint8_t)TOTAL_EFFECTS);
  uart_print_effect(effect);

  // Генерация случайных параметров для запуска эффекта:
  // thisdelay: задержка кадра анимации (0..255 мс, задает темп/скорость
  // эффекта) thishue:   базовый оттенок цвета в палитре HSV (0..255) opt:
  // случайная битовая маска флагов опций (OPT_MODE, OPT_DIR, OPT_EVEN и т.д.)
  // mmode:     младший бит подрежима (0 или 1, флаг OPT_MODE)
  // dir:       направление движения анимации (0 или OPT_DIR)
  // SEG_CNT:   число повторяющихся сегментов ленты (1..10)
  // SEG_LEN:   длина одного сегмента ленты (LED_COUNT / SEG_CNT)
  // SEG_DIR:   направление чередования сегментов ленты
  thisdelay = random8();
  thishue = random8();
  opt = random8();
  mmode = opt & OPT_MODE;
  dir = opt & OPT_DIR;
  SEG_CNT = (random8() & 7) + 1;
  SEG_LEN = LED_COUNT / SEG_CNT;
  while (1) {
    SEG_DIR = random8(1, SEG_CNT + 1);
    if (!(SEG_CNT % SEG_DIR))
      break;
  }
  // Диспетчер генерируется из двух списков (как в исходном switch): так
  // avr-gcc строит компактные таблицы переходов.
  switch (effect) {
    EFFECT_LIST_OUTER(EFFECT_SWITCH_CASE)
  default:
    // Для «симметричных» эффектов геометрия фиксирована.
    SEG_CNT = SYM_SEG_CNT;
    SEG_DIR = SYM_SEG_DIR;
    SEG_LEN = SYM_SEG_LEN;
    switch (effect) {
      EFFECT_LIST_INNER(EFFECT_SWITCH_CASE)
    }
  }

  show_transition();
}
