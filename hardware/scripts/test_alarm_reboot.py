"""Compatibility entry point for the current multi-alarm persistence tests.

The approved UI replaced the former single-alarm private globals. Test the
production FocusService, journal migration and independent process restarts.
No serial access or device flashing is performed.
"""
from verify_approved_focus_ui import main

if __name__ == '__main__':
    main()