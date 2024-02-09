# API compatibility issues

:package: **Device Model (`device_model`)**

:white_circle: **Notice:** Field `pm_base` added to `device` structure at [include/zephyr/device.h:415](https://github.com/doki-nordic/zephyr/pull/2/files#diff-6aea4492e95abb93e4d71f06c6cfe42b52c3b78a1edaa62416fc41927334d2b2R1)
〚[recomendations](#structure-field-added)〛

:white_circle: **Notice:** Field `pm_isr` added to `device` structure at [include/zephyr/device.h:416](https://github.com/doki-nordic/zephyr/pull/2/files#diff-6aea4492e95abb93e4d71f06c6cfe42b52c3b78a1edaa62416fc41927334d2b2R1)
〚[recomendations](#structure-field-added)〛

:package: **Device (`subsys_pm_device`)**

:red_circle: **Critical:** Field `usage` deleted from `pm_device` structure at [include/zephyr/device.h:416](https://github.com/doki-nordic/zephyr/pull/2/files#diff-6aea4492e95abb93e4d71f06c6cfe42b52c3b78a1edaa62416fc41927334d2b2R1)
〚[recomendations](#structure-field-deleted)〛

:red_circle: **Critical:** Field `flags` deleted from `pm_device` structure at [include/zephyr/device.h:417](https://github.com/doki-nordic/zephyr/pull/2/files#diff-6aea4492e95abb93e4d71f06c6cfe42b52c3b78a1edaa62416fc41927334d2b2R1)
〚[recomendations](#structure-field-deleted)〛

:white_circle: **Notice:** New structure "pm_device_isr" added at [include/zephyr/device.h:676](https://github.com/doki-nordic/zephyr/pull/2/files#diff-6aea4492e95abb93e4d71f06c6cfe42b52c3b78a1edaa62416fc41927334d2b2R1)
〚[recomendations](#structure-added)〛

# Issue recomendations

All the issues must be handled according to [compability guildline](#aaaa).

## :red_circle: Structure field deleted

Deleting public field in a structure break the API compatibility.
Please folow the guildlines regarding this change.

## :white_circle: Structure added

Normally adding a structure does not break an API.
In some rare cases, user is obligated to use some structures.
Make sure that it is not the case.

## :white_circle: Structure field added

Normally adding a field to a structure does not break an API, but there are some cases when it might:
* user is responsible for initializing it,
* user is obligated to handle a value from that field,
* a structure is used as binary data (e.g. it is part of some protocol), so adding a field will break binary compatibility.

If your change affects the API compability, please folow the guildlines.

