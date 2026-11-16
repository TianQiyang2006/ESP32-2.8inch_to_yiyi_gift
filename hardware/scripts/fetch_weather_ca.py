"""Obsolete: the firmware trusts `src/gift_ca.h`, not a generated weather_ca.h.

`src/gift_ca.h` is maintained by hardware/scripts/generate_gift_ca.py plus a
hand-appended cross-signed "Root YR" anchor (see the NOTE at the top of that
file). Re-running that generator would silently drop Root YR, so this old helper
must not write any CA file any more.

Keep this script as a tombstone so nobody reintroduces the stale weather_ca.h.
"""
import sys

print(__doc__)
print("fetch_weather_ca.py is obsolete: no file was written. "
      "Maintain src/gift_ca.h (generate_gift_ca.py + manual Root YR) instead.")
sys.exit(1)
