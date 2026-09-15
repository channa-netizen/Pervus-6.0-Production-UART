
from pathlib import Path

root = Path(__file__).resolve().parents[1]
receiver = (root / "usermods/GameControllerUART/GameControllerUART.cpp").read_text()
sender = (root / "Pervus_Production_Controller_ESP32_v6.ino").read_text()

# Sender emits one logical LY command, leaving effect capability to WLED.
assert 'uartDelta("LYD", c3Delta);' in sender
assert 'uartDelta("C3D", c3Delta);' not in sender

# Receiver accepts LYD and detects whether current effect exposes c3.
assert 'type == "LYD"' in receiver
assert 'extractModeDefaults(seg.mode, "c3")' in receiver
assert 'applyLyDelta' in receiver

# If c3 exists, LY changes c3; otherwise it changes intensity.
assert 'seg.custom3' in receiver
assert 'seg.intensity' in receiver

# L3 reset follows the same capability rule for LY.
assert 'resetLeftStickParameters' in receiver
assert 'extractModeDefaults(seg.mode, "ix")' in receiver

# Production sender keeps the required MIDI safety and note map.
assert 'if (velocity <= MIN_VELOCITY)' in sender
for note, preset in [(41,1),(45,2),(48,3),(52,4),(42,5),(46,6),(44,7),(54,8),
                     (37,9),(38,10),(40,11),(39,12),(49,13),(35,14),(36,15),(51,16)]:
    assert f'case {note}: return {preset};' in sender

print("LY fallback production checks passed")
