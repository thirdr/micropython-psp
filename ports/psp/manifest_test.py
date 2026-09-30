# Frozen modules for test builds (MICROPY_PSP_TEST_BUILD): the normal set,
# plus the selftest.
include("$(PORT_DIR)/manifest.py")
freeze("$(PORT_DIR)/modules-test")
