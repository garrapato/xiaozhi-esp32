#include "emote_display.h"

// Standard C++ headers
#include <cstring>
#include <memory>
#include <unordered_map>
#include <tuple>
#include <algorithm>
#include <cinttypes>

// Standard C headers
#include <sys/time.h>
#include <time.h>

// ESP-IDF headers
#include <esp_log.h>
#include <esp_lcd_panel_io.h>
#include <esp_timer.h>
#include <lvgl.h>

// FreeRTOS headers
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Project headers
#include "assets/lang_config.h"
#include "assets.h"
#include "board.h"
#include "gfx.h"
#include "expression_emote.h"


namespace emote {

// ============================================================================
// Constants and Type Definitions
// ============================================================================

static const char* TAG = "EmoteDisplay";

extern const uint8_t kotty_logo_sq_rgb565_start[] asm("_binary_kotty_logo_sq_rgb565_start");
extern const uint8_t kotty_logo_sq_rgb565_end[] asm("_binary_kotty_logo_sq_rgb565_end");

// ============================================================================
// Forward Declarations
// ============================================================================

class EmoteDisplay;

// ============================================================================
// Helper Functions
// ============================================================================

static bool OnFlushIoReady(const esp_lcd_panel_io_handle_t panel_io,
    esp_lcd_panel_io_event_data_t* const edata, void* user_ctx)
{
    emote_handle_t handle = static_cast<emote_handle_t>(user_ctx);
    if (handle) {
        emote_notify_flush_finished(handle);
    }
    return true;
}

static bool InferSquareRgb565ImageSize(const size_t data_size, int* width, int* height)
{
    if (data_size == 0 || data_size % sizeof(uint16_t) != 0) {
        return false;
    }

    const size_t pixels = data_size / sizeof(uint16_t);
    for (size_t side = 1; side * side <= pixels; ++side) {
        if (side * side == pixels) {
            *width = static_cast<int>(side);
            *height = static_cast<int>(side);
            return true;
        }
    }

    return false;
}

// Flush callback for emote
static void OnFlushCallback(int x_start, int y_start, int x_end, int y_end, const void* data, emote_handle_t handle)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)emote_get_user_data(handle);
    if (panel != nullptr) {
        esp_lcd_panel_draw_bitmap(panel, x_start, y_start, x_end, y_end, data);
    }
}

// ============================================================================
// Graphics Initialization Functions
// ============================================================================

static emote_handle_t InitializeEmote(const esp_lcd_panel_handle_t panel, const int width, const int height)
{
    if (!panel) {
        ESP_LOGE(TAG, "Invalid panel");
        return nullptr;
    }

    emote_config_t emote_cfg = {
        .flags = {
            .swap = true,
            .double_buffer = true,
            .buff_dma = false,
        },
        .gfx_emote = {
            .h_res = width,
            .v_res = height,
            .fps = 30,
        },
        .buffers = {
            .buf_pixels = static_cast<size_t>(width * 16),
        },
        .task = {
            .task_priority = 5,
            .task_stack = 6 * 1024,
            .task_affinity = 0,
            .task_stack_in_ext = false,
        },
        .flush_cb = OnFlushCallback,
        .user_data = (void*)panel,
    };

    emote_handle_t emote_handle = emote_init(&emote_cfg);
    if (!emote_handle) {
        ESP_LOGE(TAG, "Failed to initialize emote");
        return nullptr;
    }

    return emote_handle;
}

// ============================================================================
// EmoteDisplay Class Implementation
// ============================================================================

EmoteDisplay::EmoteDisplay(const esp_lcd_panel_handle_t panel, const esp_lcd_panel_io_handle_t panel_io,
                           const int width, const int height)
{
    width_ = width;
    height_ = height;
    emote_handle_ = InitializeEmote(panel, width, height);

    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = OnFlushIoReady,
    };
    esp_lcd_panel_io_register_event_callbacks(panel_io, &cbs, emote_handle_);
}

EmoteDisplay::~EmoteDisplay()
{
    if (emote_handle_) {
        emote_deinit(emote_handle_);
        emote_handle_ = nullptr;
    }
}

void EmoteDisplay::SetEmotion(const char* const emotion)
{
    ESP_LOGI(TAG, "SetEmotion: %s", emotion);
    if (emote_handle_ && emotion && strlen(emotion) > 0) {
        emote_set_anim_emoji(emote_handle_, emotion);
    }
}

void EmoteDisplay::SetChatMessage(const char* const role, const char* const content)
{
    ESP_LOGI(TAG, "SetChatMessage: %s, %s", role, content);
    if (emote_handle_ && content && strlen(content) > 0) {
        if ((std::strcmp(role, "system") == 0) && std::strstr(content, "xiaozhi.me")) {
            size_t len = strlen(content);
            char* new_content = new char[len + 1];
            strcpy(new_content, content);
            std::replace(new_content, new_content + len, static_cast<char>(0x0A), static_cast<char>(0x20));
            emote_set_event_msg(emote_handle_, EMOTE_MGR_EVT_SYS, new_content);
            delete[] new_content;
        } else {
            emote_set_event_msg(emote_handle_, EMOTE_MGR_EVT_SPEAK, content);
        }
    }
}

void EmoteDisplay::SetStatus(const char* const status)
{
    ESP_LOGI(TAG, "SetStatus: %s", status);
    if (emote_handle_ && status && strlen(status) > 0) {
        if (std::strcmp(status, Lang::Strings::LISTENING) == 0) {
            idle_status_ = false;
            notification_active_ = false;
            emote_set_event_msg(emote_handle_, EMOTE_MGR_EVT_LISTEN, NULL);
        } else if (std::strcmp(status, Lang::Strings::STANDBY) == 0) {
            idle_status_ = true;
            if (!notification_active_) {
                emote_set_event_msg(emote_handle_, EMOTE_MGR_EVT_IDLE, NULL);
            }
        } else if (std::strcmp(status, Lang::Strings::SPEAKING) == 0) {
            idle_status_ = false;
            notification_active_ = false;
            emote_set_event_msg(emote_handle_, EMOTE_MGR_EVT_SPEAK, NULL);
        } else if (std::strcmp(status, Lang::Strings::ERROR) == 0) {
            idle_status_ = false;
            notification_active_ = false;
            emote_set_event_msg(emote_handle_, EMOTE_MGR_EVT_SET, NULL);
        }
    }
}

void EmoteDisplay::ShowNotification(const char* notification, int duration_ms)
{
    ESP_LOGI(TAG, "ShowNotification: %s", notification);
    if (emote_handle_ && notification && strlen(notification) > 0) {
        notification_active_ = duration_ms > 0;
        notification_expire_time_ms_ = (esp_timer_get_time() / 1000) + duration_ms;
        emote_set_event_msg(emote_handle_, EMOTE_MGR_EVT_SYS, notification);
    }
}

void EmoteDisplay::UpdateStatusBar(bool update_all)
{
    ESP_LOGD(TAG, "UpdateStatusBar: %s", update_all ? "true" : "false");
    if (!emote_handle_) {
        return;
    }

    if (notification_active_) {
        const int64_t now_ms = esp_timer_get_time() / 1000;
        if (now_ms < notification_expire_time_ms_) {
            return;
        }
        notification_active_ = false;
    } else if (!update_all) {
        return;
    }

    if (idle_status_) {
        emote_set_event_msg(emote_handle_, EMOTE_MGR_EVT_IDLE, NULL);
    }
}

void EmoteDisplay::SetPowerSaveMode(bool on)
{
    ESP_LOGI(TAG, "SetPowerSaveMode: %s", on ? "ON" : "OFF");
    if (!emote_handle_) {
        return;
    }
}

void EmoteDisplay::ShowBootSplash(int duration_ms)
{
    if (!emote_handle_) {
        vTaskDelay(pdMS_TO_TICKS(duration_ms));
        return;
    }

    const size_t image_size = kotty_logo_sq_rgb565_end - kotty_logo_sq_rgb565_start;
    int splash_width = 0;
    int splash_height = 0;
    if (!InferSquareRgb565ImageSize(image_size, &splash_width, &splash_height)) {
        ESP_LOGW(TAG, "Invalid splash image size: %u", static_cast<unsigned>(image_size));
        vTaskDelay(pdMS_TO_TICKS(duration_ms));
        return;
    }

    if (!boot_splash_obj_) {
        boot_splash_obj_ = emote_create_obj_by_type(emote_handle_, EMOTE_OBJ_TYPE_IMAGE, "boot_splash");
    }

    if (!boot_splash_obj_) {
        ESP_LOGW(TAG, "Failed to create boot splash object");
        vTaskDelay(pdMS_TO_TICKS(duration_ms));
        return;
    }

    boot_splash_image_dsc_.header.magic = C_ARRAY_HEADER_MAGIC;
    boot_splash_image_dsc_.header.cf = GFX_COLOR_FORMAT_RGB565;
    boot_splash_image_dsc_.header.flags = 0;
    boot_splash_image_dsc_.header.w = splash_width;
    boot_splash_image_dsc_.header.h = splash_height;
    boot_splash_image_dsc_.header.stride = splash_width * sizeof(uint16_t);
    boot_splash_image_dsc_.header.reserved = 0;
    boot_splash_image_dsc_.data_size = image_size;
    boot_splash_image_dsc_.data = kotty_logo_sq_rgb565_start;
    boot_splash_image_dsc_.reserved = nullptr;
    boot_splash_image_dsc_.reserved_2 = nullptr;

    emote_lock(emote_handle_);
    gfx_img_set_src(boot_splash_obj_, &boot_splash_image_dsc_);
    gfx_obj_align(boot_splash_obj_, GFX_ALIGN_CENTER, 0, 0);
    gfx_obj_set_visible(boot_splash_obj_, true);
    emote_unlock(emote_handle_);
    RefreshAll();

    vTaskDelay(pdMS_TO_TICKS(duration_ms));

    emote_lock(emote_handle_);
    gfx_obj_set_visible(boot_splash_obj_, false);
    emote_unlock(emote_handle_);
    RefreshAll();
}

void EmoteDisplay::SetPreviewImage(const void* image)
{
    if (image) {
        ESP_LOGI(TAG, "SetPreviewImage: Preview image not supported, using default icon");
    }
}

void EmoteDisplay::SetTheme(Theme* const theme)
{
    ESP_LOGI(TAG, "SetTheme: %p", theme);
}

bool EmoteDisplay::Lock(const int timeout_ms)
{
    (void)timeout_ms;
    return true;
}

void EmoteDisplay::Unlock()
{
}

bool EmoteDisplay::StopAnimDialog()
{
    ESP_LOGI(TAG, "StopAnimDialog");
    if (emote_handle_) {
        return emote_stop_anim_dialog(emote_handle_);
    }
    return false;
}

bool EmoteDisplay::InsertAnimDialog(const char* emoji_name, uint32_t duration_ms)
{
    ESP_LOGI(TAG, "InsertAnimDialog: %s, %" PRIu32, emoji_name, duration_ms);
    if (emote_handle_ && emoji_name) {
        return emote_insert_anim_dialog(emote_handle_, emoji_name, duration_ms);
    }
    return false;
}

void EmoteDisplay::RefreshAll()
{
    if (emote_handle_) {
        emote_notify_all_refresh(emote_handle_);
        return;
    }
}

} // namespace emote
