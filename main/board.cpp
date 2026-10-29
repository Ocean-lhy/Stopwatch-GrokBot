#include "board.h"

#include "M5IOE1.h"
#include "i2c_bus.h"
#include "M5PM1.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr const char *TAG = "board";
constexpr gpio_num_t kI2cSda = GPIO_NUM_47;
constexpr gpio_num_t kI2cScl = GPIO_NUM_48;
constexpr uint8_t kIoExpanderAddr = M5IOE1_DEFAULT_ADDR_2;
constexpr uint8_t kTouchAddr = 0x15;
constexpr gpio_num_t kButtonA = GPIO_NUM_2;
constexpr gpio_num_t kButtonB = GPIO_NUM_1;

i2c_bus_handle_t s_i2c_bus = nullptr;
i2c_master_bus_handle_t s_bus = nullptr;
i2c_master_dev_handle_t s_touch = nullptr;
M5PM1 s_pm1;
M5IOE1 s_ioe;

bool set_output(uint8_t pin, uint8_t level)
{
    m5ioe1_err_t err = M5IOE1_OK;
    s_ioe.pinModeWithRes(pin, OUTPUT, &err);
    if (err != M5IOE1_OK) {
        ESP_LOGE(TAG, "IOE pin %u mode failed: %d", static_cast<unsigned>(pin + 1), err);
        return false;
    }
    s_ioe.digitalWriteWithRes(pin, level, &err);
    if (err != M5IOE1_OK) {
        ESP_LOGE(TAG, "IOE pin %u write failed: %d", static_cast<unsigned>(pin + 1), err);
        return false;
    }
    return true;
}

bool init_i2c()
{
    i2c_config_t cfg = {};
    cfg.mode = I2C_MODE_MASTER;
    cfg.sda_io_num = kI2cSda;
    cfg.scl_io_num = kI2cScl;
    cfg.sda_pullup_en = GPIO_PULLUP_ENABLE;
    cfg.scl_pullup_en = GPIO_PULLUP_ENABLE;
    cfg.master.clk_speed = 100000;
    cfg.clk_flags = 0;
    s_i2c_bus = i2c_bus_create(I2C_NUM_0, &cfg);
    if (s_i2c_bus == nullptr) {
        ESP_LOGE(TAG, "I2C bus init failed");
        return false;
    }
    s_bus = i2c_bus_get_internal_bus_handle(s_i2c_bus);
    return s_bus != nullptr;
}

bool init_pmic()
{
    M5PM1::setLogLevel(M5PM1_LOG_LEVEL_WARN);
    m5pm1_err_t err = s_pm1.begin(s_bus, M5PM1_DEFAULT_ADDR, M5PM1_I2C_FREQ_100K);
    if (err != M5PM1_OK) {
        ESP_LOGE(TAG, "PMIC init failed: %d", err);
        return false;
    }

    s_pm1.switchI2cSpeed(M5PM1_I2C_SPEED_400K);
    s_pm1.btnSetConfig(M5PM1_BTN_TYPE_CLICK, M5PM1_BTN_CLICK_DELAY_1000MS);
    s_pm1.wdtSet(0);
    s_pm1.ldoSetPowerHold(false);
    s_pm1.setPowerConfig(M5PM1_PWR_CFG_LED_CTRL, 0);
    s_pm1.setChargeEnable(true);
    s_pm1.gpioSet(M5PM1_GPIO_NUM_3, M5PM1_GPIO_MODE_OUTPUT, M5PM1_GPIO_STATE_LOW,
                  M5PM1_GPIO_PULL_NONE, M5PM1_GPIO_DRIVE_PUSHPULL);
    s_pm1.gpioSetMode(M5PM1_GPIO_NUM_2, M5PM1_GPIO_MODE_INPUT);
    return true;
}

bool init_io_expander()
{
    M5IOE1::setLogLevel(M5IOE1_LOG_LEVEL_WARN);
    m5ioe1_err_t err = s_ioe.begin(s_bus, kIoExpanderAddr, M5IOE1_I2C_FREQ_100K,
                                   M5IOE1_INT_MODE_DISABLED);
    if (err != M5IOE1_OK) {
        ESP_LOGE(TAG, "IO expander init failed: %d", err);
        return false;
    }

    s_ioe.setI2cConfig(0, M5IOE1_I2C_SPEED_400K, M5IOE1_WAKE_EDGE_RISING,
                       M5IOE1_PULL_ENABLED);

    bool ok = true;
    ok &= set_output(M5IOE1_PIN_9, LOW);   // motor enable
    ok &= set_output(M5IOE1_PIN_8, HIGH);  // display 3V3
    ok &= set_output(M5IOE1_PIN_10, LOW);  // speaker PA
    ok &= set_output(M5IOE1_PIN_4, HIGH);  // touch reset
    ok &= set_output(M5IOE1_PIN_5, HIGH);  // AMOLED reset
    ok &= set_output(M5IOE1_PIN_1, LOW);   // Grove mux
    ok &= set_output(M5IOE1_PIN_3, HIGH);  // audio rail
    s_ioe.setPwmFrequency(5000);
    return ok;
}

void init_buttons()
{
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << kButtonA) | (1ULL << kButtonB);
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
}

void init_touch()
{
    // The expander owns TP_RST. Pulse it now that the I2C bus is up.
    set_output(M5IOE1_PIN_4, LOW);
    vTaskDelay(pdMS_TO_TICKS(10));
    set_output(M5IOE1_PIN_4, HIGH);
    vTaskDelay(pdMS_TO_TICKS(60));

    i2c_device_config_t cfg = {};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = kTouchAddr;
    cfg.scl_speed_hz = 100000;
    esp_err_t err = i2c_master_bus_add_device(s_bus, &cfg, &s_touch);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Touch device add failed: %s", esp_err_to_name(err));
        s_touch = nullptr;
    }
}
}  // namespace

bool board_init()
{
    if (!init_i2c()) {
        return false;
    }
    const bool pmic_ok = init_pmic();
    const bool ioe_ok = init_io_expander();
    init_buttons();
    if (ioe_ok) {
        init_touch();
    }
    vTaskDelay(pdMS_TO_TICKS(20));
    return pmic_ok && ioe_ok;
}



bool board_touch_read(uint16_t *x, uint16_t *y)
{
    if (s_touch == nullptr || x == nullptr || y == nullptr) {
        return false;
    }
    uint8_t reg = 0x00;
    uint8_t buf[7] = {};
    esp_err_t err = i2c_master_transmit_receive(s_touch, &reg, 1, buf, sizeof(buf), 40);
    if (err != ESP_OK) {
        return false;
    }
    const uint8_t fingers = buf[2];
    const uint8_t status = (buf[3] >> 6) & 0x03;
    *x = static_cast<uint16_t>(((buf[3] & 0x0f) << 8) | buf[4]);
    *y = static_cast<uint16_t>(((buf[5] & 0x0f) << 8) | buf[6]);
    return fingers > 0 && status != 1;
}
