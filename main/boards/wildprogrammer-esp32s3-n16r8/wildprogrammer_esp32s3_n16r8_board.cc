#include "application.h"
#include "button.h"
#include "codecs/no_audio_codec.h"
#include "config.h"
#include "display/oled_display.h"
#include "led/single_led.h"
#include "mcp_server.h"
#include "wifi_board.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdint>

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>

namespace {

constexpr char TAG[] = "WildprogrammerAudio";

std::string VolumeResponse(int volume, const char* prefix = "当前音量是") {
    return std::string("{\"action\":\"RESPONSE\",\"response\":\"") + prefix +
           std::to_string(volume) + "。\"}";
}

// The microphone and amplifier use independent I2S controllers, matching the
// upstream bread-compact-wifi wiring. Diagnostics expose counters only; they
// never persist, stream, or return PCM audio.
class SimplexDiagnosticCodec : public NoAudioCodecSimplex {
private:
    uint64_t mic_square_sum_ = 0;
    uint32_t mic_sample_count_ = 0;
    int32_t mic_peak_ = 0;
    uint32_t empty_read_count_ = 0;
    std::atomic<uint32_t> total_empty_reads_{0};
    std::atomic<int32_t> last_mic_rms_tenths_{0};
    std::atomic<int32_t> last_mic_peak_{0};
    std::atomic<bool> pcm_seen_{false};

protected:
    int Read(int16_t* dest, int samples) override {
        int count = NoAudioCodec::Read(dest, samples);
        if (count <= 0) {
            total_empty_reads_.fetch_add(1, std::memory_order_relaxed);
            if (++empty_read_count_ % 5 == 0) {
                ESP_LOGW(TAG, "MIC_DIAG no PCM samples received");
            }
            return count;
        }

        empty_read_count_ = 0;
        pcm_seen_.store(true, std::memory_order_relaxed);
        for (int i = 0; i < count; ++i) {
            int32_t value = dest[i];
            int32_t magnitude = value == INT16_MIN ? 32768 : std::abs(value);
            mic_peak_ = std::max(mic_peak_, magnitude);
            mic_square_sum_ += static_cast<int64_t>(value) * value;
        }
        mic_sample_count_ += count;

        if (mic_sample_count_ >= AUDIO_INPUT_SAMPLE_RATE) {
            double rms = std::sqrt(static_cast<double>(mic_square_sum_) /
                                   mic_sample_count_);
            ESP_LOGW(TAG, "MIC_DIAG samples=%lu rms=%.1f peak=%ld",
                     static_cast<unsigned long>(mic_sample_count_), rms,
                     static_cast<long>(mic_peak_));
            last_mic_rms_tenths_.store(
                static_cast<int32_t>(std::lround(rms * 10.0)),
                std::memory_order_relaxed);
            last_mic_peak_.store(mic_peak_, std::memory_order_relaxed);
            mic_square_sum_ = 0;
            mic_sample_count_ = 0;
            mic_peak_ = 0;
        }
        return count;
    }

public:
    SimplexDiagnosticCodec()
        : NoAudioCodecSimplex(
              AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
              AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK,
              AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK,
              AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN) {}

    std::string DiagnosticsJson() const {
        char json[512];
        const double last_rms =
            last_mic_rms_tenths_.load(std::memory_order_relaxed) / 10.0;
        snprintf(
            json, sizeof(json),
            "{\"input_enabled\":%s,\"output_enabled\":%s,\"duplex\":%s,"
            "\"pcm_seen\":%s,\"input_sample_rate\":%d,"
            "\"output_sample_rate\":%d,\"volume\":%d,\"input_gain\":%.2f,"
            "\"last_rms\":%.1f,\"last_peak\":%ld,\"empty_reads\":%lu}",
            input_enabled() ? "true" : "false",
            output_enabled() ? "true" : "false", duplex() ? "true" : "false",
            pcm_seen_.load(std::memory_order_relaxed) ? "true" : "false",
            input_sample_rate(), output_sample_rate(), output_volume(), input_gain(),
            last_rms,
            static_cast<long>(last_mic_peak_.load(std::memory_order_relaxed)),
            static_cast<unsigned long>(
                total_empty_reads_.load(std::memory_order_relaxed)));
        return json;
    }
};

}  // namespace

class WildprogrammerEsp32s3N16r8Board : public WifiBoard {
private:
    Button boot_button_;
    i2c_master_bus_handle_t display_i2c_bus_ = nullptr;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    Display* display_ = nullptr;

protected:
    const char* GetWifiSsidPrefix() const override {
        return "Lingxi";
    }

private:
    void UseNoDisplay(const char* step, esp_err_t error) {
        ESP_LOGE(TAG, "OLED unavailable at %s: %s", step, esp_err_to_name(error));
        display_ = new NoDisplay();
    }

    void InitializeDisplay() {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = static_cast<i2c_port_t>(0),
            .sda_io_num = DISPLAY_SDA_PIN,
            .scl_io_num = DISPLAY_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };

        esp_err_t result = i2c_new_master_bus(&bus_config, &display_i2c_bus_);
        if (result != ESP_OK) {
            UseNoDisplay("I2C bus creation", result);
            return;
        }

        uint8_t address = 0x3C;
        result = i2c_master_probe(display_i2c_bus_, address, 100);
        if (result != ESP_OK) {
            address = 0x3D;
            result = i2c_master_probe(display_i2c_bus_, address, 100);
        }
        if (result != ESP_OK) {
            UseNoDisplay("I2C probe (0x3C/0x3D)", result);
            return;
        }
        ESP_LOGI(TAG, "OLED detected at I2C address 0x%02X", address);

        esp_lcd_panel_io_i2c_config_t io_config = {};
        io_config.dev_addr = address;
        io_config.scl_speed_hz = 400 * 1000;
        io_config.control_phase_bytes = 1;
        io_config.dc_bit_offset = 6;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        io_config.flags.dc_low_on_data = 0;
        io_config.flags.disable_control_phase = 0;

        result = esp_lcd_new_panel_io_i2c(display_i2c_bus_, &io_config, &panel_io_);
        if (result != ESP_OK) {
            UseNoDisplay("panel IO creation", result);
            return;
        }

        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = static_cast<uint8_t>(DISPLAY_HEIGHT),
        };
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.bits_per_pixel = 1;
        panel_config.vendor_config = &ssd1306_config;

        result = esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_);
        if (result != ESP_OK) {
            UseNoDisplay("SSD1306 driver creation", result);
            return;
        }
        if ((result = esp_lcd_panel_reset(panel_)) != ESP_OK ||
            (result = esp_lcd_panel_init(panel_)) != ESP_OK ||
            (result = esp_lcd_panel_invert_color(panel_, false)) != ESP_OK ||
            (result = esp_lcd_panel_disp_on_off(panel_, true)) != ESP_OK) {
            UseNoDisplay("SSD1306 initialization", result);
            return;
        }

        display_ = new OledDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                   DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        ESP_LOGI(TAG, "SSD1306 %dx%d display initialized", DISPLAY_WIDTH, DISPLAY_HEIGHT);
    }

    void InitializeButton() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
    }

    void InitializeTools() {
        auto& mcp = McpServer::GetInstance();
        mcp.AddTool(
            "self.audio.get_diagnostics",
            "Read bounded audio health metrics. Never returns or stores raw audio.",
            PropertyList(), [this](const PropertyList&) -> ReturnValue {
                return static_cast<SimplexDiagnosticCodec*>(GetAudioCodec())
                    ->DiagnosticsJson();
            });
        mcp.AddTool(
            "self.audio_speaker.get_volume",
            "Read the speaker volume from the device. Returns the verified value from 0 to 100.",
            PropertyList(), [this](const PropertyList&) -> ReturnValue {
                return VolumeResponse(GetAudioCodec()->output_volume());
            });
        mcp.AddTool(
            "self.audio_speaker.set_volume_verified",
            "Set speaker volume to an absolute value from 0 to 100 and read it back.",
            PropertyList({Property("volume", kPropertyTypeInteger, 0, 100)}),
            [this](const PropertyList& properties) -> ReturnValue {
                auto codec = GetAudioCodec();
                codec->SetOutputVolume(properties["volume"].value<int>());
                return VolumeResponse(codec->output_volume(), "音量已设置为");
            });
        mcp.AddTool(
            "self.audio_speaker.adjust_volume",
            "Adjust speaker volume by percentage points, clamp to 0..100, and read it back.",
            PropertyList({Property("delta", kPropertyTypeInteger, -100, 100)}),
            [this](const PropertyList& properties) -> ReturnValue {
                auto codec = GetAudioCodec();
                const int current = codec->output_volume();
                const int target = std::clamp(
                    current + properties["delta"].value<int>(), 0, 100);
                codec->SetOutputVolume(target);
                return VolumeResponse(codec->output_volume(), "音量已调整为");
            });
    }

public:
    WildprogrammerEsp32s3N16r8Board() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeDisplay();
        InitializeButton();
        InitializeTools();
    }

    AudioCodec* GetAudioCodec() override {
        static SimplexDiagnosticCodec audio_codec;
        return &audio_codec;
    }

    Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    Display* GetDisplay() override {
        return display_;
    }
};

DECLARE_BOARD(WildprogrammerEsp32s3N16r8Board);
