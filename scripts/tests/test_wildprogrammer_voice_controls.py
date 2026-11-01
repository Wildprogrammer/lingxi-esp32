"""Source contract tests for Lingxi local controls and verified volume tools."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
BOARD = (
    ROOT
    / "main"
    / "boards"
    / "wildprogrammer-esp32s3-n16r8"
    / "wildprogrammer_esp32s3_n16r8_board.cc"
)


class WildprogrammerVoiceControlTests(unittest.TestCase):
    def test_public_build_configuration_contains_no_machine_specific_endpoint(self):
        board_dir = BOARD.parent
        build_script = (board_dir / "build.ps1").read_text(encoding="utf-8")
        default_config = (board_dir / "config.json").read_text(encoding="utf-8")
        local_example = (board_dir / "config.local.json.example").read_text(
            encoding="utf-8"
        )
        gitignore = (ROOT / ".gitignore").read_text(encoding="utf-8")

        self.assertIn("IDF_POWERSHELL_PROFILE", build_script)
        self.assertIn("--config $Config", build_script)
        self.assertNotIn("D:\\install", build_script)
        self.assertNotIn("192.168.", default_config)
        self.assertIn("YOUR_SERVER_HOST", local_example)
        self.assertIn(
            "main/boards/wildprogrammer-esp32s3-n16r8/config.local.json",
            gitignore,
        )

    def test_wake_words_include_lingxi_aliases_at_tuned_sensitivity(self):
        build_script = (
            ROOT
            / "main"
            / "boards"
            / "wildprogrammer-esp32s3-n16r8"
            / "build.ps1"
        ).read_text(encoding="utf-8")
        generic_build = (ROOT / "scripts/build.py").read_text(encoding="utf-8")

        for phrase in ("你好灵溪", "小灵溪", "灵溪灵溪", "灵溪你好", "灵溪在吗"):
            self.assertIn(phrase, build_script)
        self.assertIn("custom_threshold: int = 20", generic_build)
        self.assertIn(
            'f"CONFIG_CUSTOM_WAKE_WORD_THRESHOLD={custom_threshold}"',
            generic_build,
        )
        self.assertIn("[int]$WakeWordThreshold = 25", build_script)
        self.assertIn("--wake-word-threshold $WakeWordThreshold", build_script)

    def test_board_exposes_read_adjust_and_verified_volume_tools(self):
        source = BOARD.read_text(encoding="utf-8")

        for tool in (
            "self.audio_speaker.get_volume",
            "self.audio_speaker.set_volume_verified",
            "self.audio_speaker.adjust_volume",
        ):
            self.assertIn(tool, source)
        self.assertIn("codec->output_volume()", source)
        self.assertIn("std::clamp", source)
        self.assertIn("codec->SetOutputVolume", source)
        self.assertIn('\\"action\\":\\"RESPONSE\\"', source)
        self.assertIn("InitializeTools();", source)

    def test_audio_diagnostics_are_bounded_and_do_not_play_an_automatic_tone(self):
        source = BOARD.read_text(encoding="utf-8")

        self.assertIn('"self.audio.get_diagnostics"', source)
        for metric in (
            '\\"pcm_seen\\"',
            '\\"last_rms\\"',
            '\\"last_peak\\"',
            '\\"empty_reads\\"',
        ):
            self.assertIn(metric, source)
        self.assertIn("std::atomic<bool> pcm_seen_", source)
        self.assertIn("std::atomic<uint32_t> total_empty_reads_", source)
        self.assertIn("Never returns or stores raw audio", source)
        self.assertNotIn("StartDiagnosticTone", source)
        self.assertNotIn("SPEAKER_DIAG", source)
        self.assertNotIn("tone_started", source)

    def test_spoken_stop_commands_are_sent_directly_while_speaking(self):
        application = (ROOT / "main/application.cc").read_text(encoding="utf-8")
        board = BOARD.read_text(encoding="utf-8")
        build_script = (
            ROOT
            / "main"
            / "boards"
            / "wildprogrammer-esp32s3-n16r8"
            / "build.ps1"
        ).read_text(encoding="utf-8")

        for phrase in ("停止", "闭嘴", "退出"):
            self.assertIn(phrase, application)
            self.assertIn(phrase, build_script)
        self.assertIn("IsLocalVoiceControlCommand", application)
        self.assertIn("protocol_->SendWakeWordDetected(wake_word)", application)

        handler_start = application.index("void Application::HandleWakeWordDetectedEvent()")
        handler_end = application.index("void Application::BeginWakeWordInvoke(", handler_start)
        handler = application[handler_start:handler_end]
        local_start = handler.index("IsLocalVoiceControlCommand(wake_word)")
        local_end = handler.index("if (state == kDeviceStateIdle)", local_start)
        local_branch = handler[local_start:local_end]
        ordered_calls = (
            "AbortSpeaking(kAbortReasonWakeWordDetected)",
            "audio_service_.PopPacketFromSendQueue()",
            "protocol_->SendWakeWordDetected(wake_word)",
            "SetDeviceState(kDeviceStateIdle)",
            "audio_service_.EnableWakeWordDetection(true)",
        )
        positions = [local_branch.index(call) for call in ordered_calls]
        self.assertEqual(positions, sorted(positions))

        idle_control_guard = (
            "if (state == kDeviceStateIdle && "
            "IsLocalVoiceControlCommand(wake_word))"
        )
        self.assertIn(idle_control_guard, handler)
        idle_guard_start = handler.index(idle_control_guard)
        idle_invoke_start = handler.index(
            "if (state == kDeviceStateIdle) {", idle_guard_start + len(idle_control_guard)
        )
        idle_guard = handler[idle_guard_start:idle_invoke_start]
        self.assertIn("audio_service_.EnableWakeWordDetection(true)", idle_guard)
        self.assertIn("return;", idle_guard)
        self.assertNotIn("BeginWakeWordInvoke", idle_guard)
        self.assertNotIn("SendWakeWordDetected", idle_guard)

        self.assertIn("boot_button_.OnClick", board)
        self.assertIn("app.ToggleChatState();", board)
        toggle_start = application.index("void Application::HandleToggleChatEvent()")
        toggle_end = application.index("void Application::ContinueOpenAudioChannel(", toggle_start)
        toggle_handler = application[toggle_start:toggle_end]
        self.assertIn("state == kDeviceStateSpeaking", toggle_handler)
        self.assertIn("AbortSpeaking(kAbortReasonNone)", toggle_handler)
        self.assertIn("state == kDeviceStateListening", toggle_handler)
        self.assertIn("protocol_->CloseAudioChannel()", toggle_handler)


if __name__ == "__main__":
    unittest.main()
