"""Back up every partition of the previous 4MB-layout test firmware.

ROM reads avoid dropped USB packets in this Windows connection's stub reads.
The chip has 16MB flash; the previous partition table ends at 4MB.
The backup includes bootloader, NVS, OTA slots, filesystem and coredump.
"""
import hashlib
import sys
from pathlib import Path
import esptool

if __name__=='__main__':
    out=Path(__file__).resolve().parents[2]/'build/backups/esp32s3-before-lvgl-first4mb-20261006.bin'
    out.parent.mkdir(parents=True,exist_ok=True)
    esptool.main(['--chip','esp32s3','--port',sys.argv[1] if len(sys.argv)>1 else 'COM8',
                  '--no-stub','read_flash','0','0x400000',str(out)])
    data=out.read_bytes()
    assert len(data)==4194304
    out.with_suffix('.sha256').write_text(hashlib.sha256(data).hexdigest()+'\n',encoding='ascii')
    print('4MB test firmware partition backup saved')
