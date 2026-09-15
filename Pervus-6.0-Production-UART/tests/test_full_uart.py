from pathlib import Path
root = Path(__file__).resolve().parents[1]
receiver = (root/'usermods/GameControllerUART/GameControllerUART.cpp').read_text()
sender = (root/'Pervus_Production_Controller_ESP32_v6.ino').read_text()

assert 'esp_now' not in sender.lower()
assert '#include <WiFi.h>' not in sender
assert 'sendPresetUart((uint8_t)preset);' in sender
for p in range(1,11):
    assert f'sendPresetUart({p})' in sender
for cmd in ['MAP:-1','MAP:1','RND:1','STB:1','STB:0','RSTL:1','RSTR:1']:
    assert cmd in sender
for cmd in ['SXD','LYD','C1D','C2D']:
    assert f'uartDelta("{cmd}"' in sender
for typ in ['"MAP"','"RND"','"RSTL"','"RSTR"','"STB"','"P"']:
    assert typ in receiver
assert 'constexpr int MAP_COUNT = 5' in receiver
assert 'extractModeDefaults(seg.mode, "sx")' in receiver
assert 'extractModeDefaults(seg.mode, "c3")' in receiver
print('full UART static checks passed')
