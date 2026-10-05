/*
 * Copyright (c) 2020-2023 ndeadly
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "gamesir_controller.hpp"
#include <stratosphere.hpp>
#include <algorithm>
#include <cstring>

namespace ams::controller {

    namespace {

        const constexpr float stick_scale_factor = float(UINT12_MAX) / UINT8_MAX;

        // LSM6 / ICM / QMI on G7 Pro: ±16 g and ±2000 dps, 16-bit.
        // Convert into the units EmulatedSwitchController copies into report 0x30.
        constexpr float acc_scale  = 4000.0f / 2048.0f;
        constexpr float gyro_scale = 936.0f / 16.384f;

        constexpr size_t android_hid_core_size = 10; // id + 9-byte 0x07 body
        constexpr size_t imu_sample_size = 12;
        constexpr u8 hid_key_printscreen = 0x46;
        constexpr u8 capture_hold_reports = 200; // ~2s of 0x07 if no key-up report

        struct GamesirImuSample {
            s16 acc_x;
            s16 acc_y;
            s16 acc_z;
            s16 gyro_x;
            s16 gyro_y;
            s16 gyro_z;
        } PACKED;

    }

    Result GamesirController::Initialize() {
        R_TRY(EmulatedSwitchController::Initialize());
        this->EnableExtendedFeatures();
        R_SUCCEED();
    }

    void GamesirController::EnableExtendedFeatures() {
        // Output 0x12/0x14/0x0f (63 bytes) and 0x05 (32 bytes) exist on the
        // extended Android HID descriptor. Short 11-byte Android HID ignores them.
        u8 enable[63]{};
        enable[0] = 0x01;
        enable[1] = 0x01;
        enable[2] = 0x01;
        enable[3] = 0x01;
        this->SendRawOutput(0x12, enable, 63);
        this->SendRawOutput(0x14, enable, 63);
        this->SendRawOutput(0x0f, enable, 63);
        this->SendRawOutput(0x05, enable, 32);
        this->PushRumbleState(0, 0, 0, 0);
    }

    Result GamesirController::SendRawOutput(u8 report_id, const u8 *payload, size_t payload_size) {
        std::scoped_lock lk(m_output_mutex);
        std::memset(m_output_report.data, 0, sizeof(m_output_report.data));
        m_output_report.data[0] = report_id;
        if (payload && payload_size) {
            const size_t n = std::min(payload_size, sizeof(m_output_report.data) - 1);
            std::memcpy(m_output_report.data + 1, payload, n);
            m_output_report.size = static_cast<u16>(n + 1);
        } else {
            m_output_report.size = 1;
        }
        this->WriteDataReport(&m_output_report);
        R_SUCCEED();
    }

    Result GamesirController::SetVibration(const SwitchRumbleData *rumble_data) {
        const float left_amp  = std::max(rumble_data[0].low_band_amp,  rumble_data[0].high_band_amp);
        const float right_amp = std::max(rumble_data[1].low_band_amp,  rumble_data[1].high_band_amp);
        m_rumble_left  = static_cast<u8>(255.0f * std::clamp(left_amp,  0.0f, 1.0f));
        m_rumble_right = static_cast<u8>(255.0f * std::clamp(right_amp, 0.0f, 1.0f));
        m_rumble_lt    = static_cast<u8>(255.0f * std::clamp(rumble_data[0].low_band_amp,  0.0f, 1.0f));
        m_rumble_rt    = static_cast<u8>(255.0f * std::clamp(rumble_data[1].high_band_amp, 0.0f, 1.0f));
        R_RETURN(this->PushRumbleState(m_rumble_left, m_rumble_right, m_rumble_lt, m_rumble_rt));
    }

    Result GamesirController::CancelVibration() {
        m_rumble_left = m_rumble_right = m_rumble_lt = m_rumble_rt = 0;
        R_RETURN(this->PushRumbleState(0, 0, 0, 0));
    }

    Result GamesirController::PushRumbleState(u8 left, u8 right, u8 trigger_l, u8 trigger_r) {
        std::scoped_lock lk(m_output_mutex);

        auto write = [&](size_t size) {
            m_output_report.size = static_cast<u16>(size);
            this->WriteDataReport(&m_output_report);
        };

        u8 *out = m_output_report.data;

        // Extended Android HID: output 0x05, 32 vendor bytes (PWM four motors).
        std::memset(out, 0, 33);
        out[0] = 0x05;
        out[1] = left;
        out[2] = right;
        out[3] = trigger_l;
        out[4] = trigger_r;
        write(33);

        // Xbox-style interrupt rumble, magnitudes 0-100.
        std::memset(out, 0, 9);
        out[0] = 0x03;
        out[1] = 0x0f;
        out[2] = static_cast<u8>(left * 100u / 255u);
        out[3] = static_cast<u8>(right * 100u / 255u);
        out[4] = static_cast<u8>(trigger_l * 100u / 255u);
        out[5] = static_cast<u8>(trigger_r * 100u / 255u);
        out[6] = 1;
        write(9);

        R_SUCCEED();
    }

    void GamesirController::ApplyFaceButtons(bool south, bool east, bool west, bool north) {
        // G7 face letters are Xbox: south=A east=B west=X north=Y.
        // Map by label so printed A/B/X/Y match Switch A/B/X/Y.
        m_buttons.A = south;
        m_buttons.B = east;
        m_buttons.X = west;
        m_buttons.Y = north;
    }

    void GamesirController::ProcessInputData(const bluetooth::HidReport *report) {
        auto gamesir_report = reinterpret_cast<const GamesirReportData *>(&report->data);
        const u8 *bytes = reinterpret_cast<const u8 *>(&report->data);
        const size_t size = report->size;

        switch(gamesir_report->id) {
            case 0x01:
                // G7 Pro BT: 0x01 is the keyboard collection (Share = PrintScreen).
                // Older GameSir pads used 0x01 as a gamepad frame.
                if (m_id.vid == 0x3537) {
                    this->MapKeyboardReport(bytes, size);
                } else {
                    this->MapInputReport0x07(gamesir_report);
                }
                break;
            case 0x07:
                this->MapInputReport0x07(gamesir_report);
                if (size > android_hid_core_size) {
                    this->MapVendorImu(bytes + android_hid_core_size, size - android_hid_core_size);
                }
                break;
            case 0x02:
                this->MapInputReport0x02(gamesir_report); break;
            case 0x03:
                this->MapInputReport0x03(gamesir_report); break;
            case 0x10:
            case 0x14:
                if (size > 1) {
                    this->MapVendorImu(bytes + 1, size - 1);
                }
                break;
            case 0x12:
                if (size <= 4) {
                    this->MapInputReport0x12(gamesir_report);
                } else {
                    this->MapVendorImu(bytes + 1, size - 1);
                }
                break;
            case 0x21:
            case 0x30:
            case 0x31:
            case 0x3f:
                this->MapNintendoInputReport(report); break;
            case 0xc4:
                this->MapInputReport0xc4(gamesir_report); break;
            default:
                if ((size >= 10) && (size <= 16) && (gamesir_report->id != 0x01) && (gamesir_report->id != 0x06)) {
                    this->MapInputReport0x07(gamesir_report);
                }
                break;
        }

        this->ApplyLatchedShareButtons();
    }

    void GamesirController::MapKeyboardReport(const u8 *bytes, size_t size) {
        bool printscreen = false;
        // Report 0x01: modifiers, reserved, then 6–10 boot keycodes (PrintScreen = 0x46).
        for (size_t i = 1; i < size; ++i) {
            if (bytes[i] == hid_key_printscreen) {
                printscreen = true;
                break;
            }
        }
        m_capture_ttl = printscreen ? capture_hold_reports : 0;
    }

    void GamesirController::ApplyLatchedShareButtons() {
        m_buttons.capture = m_capture_ttl != 0;
        if (m_capture_ttl > 0) {
            m_capture_ttl--;
        }
    }

    void GamesirController::MapInputReport0x03(const GamesirReportData *src) {
        m_left_stick.SetData(
            static_cast<u16>(stick_scale_factor * src->input0x03.left_stick.x) & UINT12_MAX,
            static_cast<u16>(stick_scale_factor * (UINT8_MAX - src->input0x03.left_stick.y)) & UINT12_MAX
        );
        m_right_stick.SetData(
            static_cast<u16>(stick_scale_factor * src->input0x03.right_stick.x) & UINT12_MAX,
            static_cast<u16>(stick_scale_factor * (UINT8_MAX - src->input0x03.right_stick.y)) & UINT12_MAX
        );

        m_buttons.dpad_down  = (src->input0x03.buttons.dpad == GamesirDpad2_S)  ||
                               (src->input0x03.buttons.dpad == GamesirDpad2_SE) ||
                               (src->input0x03.buttons.dpad == GamesirDpad2_SW);
        m_buttons.dpad_up    = (src->input0x03.buttons.dpad == GamesirDpad2_N)  ||
                               (src->input0x03.buttons.dpad == GamesirDpad2_NE) ||
                               (src->input0x03.buttons.dpad == GamesirDpad2_NW);
        m_buttons.dpad_right = (src->input0x03.buttons.dpad == GamesirDpad2_E)  ||
                               (src->input0x03.buttons.dpad == GamesirDpad2_NE) ||
                               (src->input0x03.buttons.dpad == GamesirDpad2_SE);
        m_buttons.dpad_left  = (src->input0x03.buttons.dpad == GamesirDpad2_W)  ||
                               (src->input0x03.buttons.dpad == GamesirDpad2_NW) ||
                               (src->input0x03.buttons.dpad == GamesirDpad2_SW);

        this->ApplyFaceButtons(src->input0x03.buttons.A, src->input0x03.buttons.B,
                               src->input0x03.buttons.X, src->input0x03.buttons.Y);

        m_buttons.R  = src->input0x03.buttons.RB;
        m_buttons.ZR = src->input0x03.right_trigger > (m_trigger_threshold * UINT8_MAX);
        m_buttons.L  = src->input0x03.buttons.LB;
        m_buttons.ZL = src->input0x03.left_trigger  > (m_trigger_threshold * UINT8_MAX);

        m_buttons.minus = src->input0x03.buttons.select;
        m_buttons.plus  = src->input0x03.buttons.start;

        m_buttons.lstick_press = src->input0x03.buttons.L3;
        m_buttons.rstick_press = src->input0x03.buttons.R3;

        if (src->input0x03.buttons.home) {
            m_buttons.home = 1;
        }
    }

    void GamesirController::MapInputReport0x07(const GamesirReportData *src) {
        m_left_stick.SetData(
            static_cast<u16>(stick_scale_factor * src->input0x07.left_stick.x) & UINT12_MAX,
            static_cast<u16>(stick_scale_factor * (UINT8_MAX - src->input0x07.left_stick.y)) & UINT12_MAX
        );
        m_right_stick.SetData(
            static_cast<u16>(stick_scale_factor * src->input0x07.right_stick.x) & UINT12_MAX,
            static_cast<u16>(stick_scale_factor * (UINT8_MAX - src->input0x07.right_stick.y)) & UINT12_MAX
        );

        m_buttons.dpad_down  = (src->input0x07.dpad == GamesirDpad2_S)  ||
                               (src->input0x07.dpad == GamesirDpad2_SE) ||
                               (src->input0x07.dpad == GamesirDpad2_SW);
        m_buttons.dpad_up    = (src->input0x07.dpad == GamesirDpad2_N)  ||
                               (src->input0x07.dpad == GamesirDpad2_NE) ||
                               (src->input0x07.dpad == GamesirDpad2_NW);
        m_buttons.dpad_right = (src->input0x07.dpad == GamesirDpad2_E)  ||
                               (src->input0x07.dpad == GamesirDpad2_NE) ||
                               (src->input0x07.dpad == GamesirDpad2_SE);
        m_buttons.dpad_left  = (src->input0x07.dpad == GamesirDpad2_W)  ||
                               (src->input0x07.dpad == GamesirDpad2_NW) ||
                               (src->input0x07.dpad == GamesirDpad2_SW);

        this->ApplyFaceButtons(src->input0x07.A, src->input0x07.B,
                               src->input0x07.X, src->input0x07.Y);

        m_buttons.R  = src->input0x07.RB;
        m_buttons.ZR = src->input0x07.right_trigger > (m_trigger_threshold * UINT8_MAX);
        m_buttons.L  = src->input0x07.LB;
        m_buttons.ZL = src->input0x07.left_trigger  > (m_trigger_threshold * UINT8_MAX);

        m_buttons.minus = src->input0x07.select;
        m_buttons.plus  = src->input0x07.start;

        m_buttons.lstick_press = src->input0x07.L3;
        m_buttons.rstick_press = src->input0x07.R3;

        // Xbox/Home is a separate consumer report 0x02 (bit 0x80). 0x07 never
        // carries it; writing home here would immediately clear a real press.
        if (src->input0x07.home) {
            m_buttons.home = 1;
        }
    }

    void GamesirController::MapInputReport0x02(const GamesirReportData *src) {
        AMS_UNUSED(src);
        const u8 *bytes = reinterpret_cast<const u8 *>(src);
        m_buttons.home = (bytes[1] & 0x80) != 0;
    }

    void GamesirController::MapNintendoInputReport(const bluetooth::HidReport *report) {
        auto sw = reinterpret_cast<const SwitchInputReport *>(&report->data);
        m_buttons = sw->buttons;
        m_left_stick = sw->left_stick;
        m_right_stick = sw->right_stick;
        if (m_enable_motion && (report->size >= 0x31)) {
            std::memcpy(m_motion_data, sw->type0x30.motion_data, sizeof(m_motion_data));
        }
    }

    void GamesirController::MapVendorImu(const u8 *payload, size_t length) {
        if (!m_enable_motion || (length < imu_sample_size)) {
            return;
        }

        size_t offset = 0;
        if ((length >= (imu_sample_size + 2)) && ((length % imu_sample_size) == 2)) {
            offset = 2;
        }

        const size_t available = (length - offset) / imu_sample_size;
        const size_t count = std::min<size_t>(available, 3);
        for (size_t i = 0; i < count; ++i) {
            GamesirImuSample sample{};
            std::memcpy(&sample, payload + offset + (i * imu_sample_size), sizeof(sample));

            m_motion_data[i].accel_x = static_cast<s16>(acc_scale  * (-sample.acc_z));
            m_motion_data[i].accel_y = static_cast<s16>(acc_scale  * (-sample.acc_x));
            m_motion_data[i].accel_z = static_cast<s16>(acc_scale  *   sample.acc_y);
            m_motion_data[i].gyro_1  = static_cast<s16>(gyro_scale * (-sample.gyro_z));
            m_motion_data[i].gyro_2  = static_cast<s16>(gyro_scale * (-sample.gyro_x));
            m_motion_data[i].gyro_3  = static_cast<s16>(gyro_scale *   sample.gyro_y);
        }
        for (size_t i = count; i < 3; ++i) {
            m_motion_data[i] = m_motion_data[0];
        }
    }

    void GamesirController::MapInputReport0x12(const GamesirReportData *src) {
        const u8 *bytes = reinterpret_cast<const u8 *>(src);
        m_buttons.home = src->input0x12.home || ((bytes[1] & 0x80) != 0);
    }

    void GamesirController::MapInputReport0xc4(const GamesirReportData *src) {
        m_left_stick.SetData(
            static_cast<u16>(stick_scale_factor * src->input0xc4.left_stick.x) & UINT12_MAX,
            static_cast<u16>(stick_scale_factor * (UINT8_MAX - src->input0xc4.left_stick.y)) & UINT12_MAX
        );
        m_right_stick.SetData(
            static_cast<u16>(stick_scale_factor * src->input0xc4.right_stick.x) & UINT12_MAX,
            static_cast<u16>(stick_scale_factor * (UINT8_MAX - src->input0xc4.right_stick.y)) & UINT12_MAX
        );

        m_buttons.dpad_down   = (src->input0xc4.buttons.dpad == GamesirDpad_S)  ||
                                (src->input0xc4.buttons.dpad == GamesirDpad_SE) ||
                                (src->input0xc4.buttons.dpad == GamesirDpad_SW);
        m_buttons.dpad_up     = (src->input0xc4.buttons.dpad == GamesirDpad_N)  ||
                                (src->input0xc4.buttons.dpad == GamesirDpad_NE) ||
                                (src->input0xc4.buttons.dpad == GamesirDpad_NW);
        m_buttons.dpad_right  = (src->input0xc4.buttons.dpad == GamesirDpad_E)  ||
                                (src->input0xc4.buttons.dpad == GamesirDpad_NE) ||
                                (src->input0xc4.buttons.dpad == GamesirDpad_SE);
        m_buttons.dpad_left   = (src->input0xc4.buttons.dpad == GamesirDpad_W)  ||
                                (src->input0xc4.buttons.dpad == GamesirDpad_NW) ||
                                (src->input0xc4.buttons.dpad == GamesirDpad_SW);

        this->ApplyFaceButtons(src->input0xc4.buttons.A, src->input0xc4.buttons.B,
                               src->input0xc4.buttons.X, src->input0xc4.buttons.Y);

        m_buttons.R  = src->input0xc4.buttons.RB;
        m_buttons.ZR = src->input0xc4.right_trigger > (m_trigger_threshold * UINT8_MAX);
        m_buttons.L  = src->input0xc4.buttons.LB;
        m_buttons.ZL = src->input0xc4.left_trigger  > (m_trigger_threshold * UINT8_MAX);

        m_buttons.minus = src->input0xc4.buttons.select;
        m_buttons.plus  = src->input0xc4.buttons.start;

        m_buttons.lstick_press = src->input0xc4.buttons.L3;
        m_buttons.rstick_press = src->input0xc4.buttons.R3;
    }

}
