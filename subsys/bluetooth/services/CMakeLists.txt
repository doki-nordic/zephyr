# SPDX-License-Identifier: Apache-2.0

zephyr_link_libraries(subsys__bluetooth)

zephyr_sources_ifdef(CONFIG_BT_GATT_DIS dis.c)

zephyr_sources_ifdef(CONFIG_BT_GATT_BAS bas.c)

zephyr_sources_ifdef(CONFIG_BT_GATT_HRS hrs.c)
