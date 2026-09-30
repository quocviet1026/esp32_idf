---
name: new-component
description: 'Scaffold a new ESP-IDF component matching this project''s structure (include/, internal header, Kconfig, CMakeLists, idempotent init/deinit, host test). Use when adding a new driver or service.'
argument-hint: '<component_name> <driver|service> <responsibility description>'
---

# Create a new component

Before generating any files, **confirm with the user** (if the argument doesn't cover it):
the component name, its layer (driver/service), a one-sentence responsibility, whether it has
multiple instances (→ opaque handle), whether it owns a task (stack/priority), what events it
emits, and which components it depends on.
Search `components/` for an existing component that already does something similar — if one exists,
suggest extending it instead of creating a new one.

## Generated structure

```
components/<name>/
├── CMakeLists.txt          # minimal REQUIRES, -Wall -Wextra -Werror
├── Kconfig                 # every hardware constant/tuning value, with range + help
├── include/<name>.h        # public API, fully commented
├── <name>_internal.h       # internal types/functions (if needed)
├── <name>.c
└── test/host/test_<name>.c # Unity test for the pure logic
```

## Required API skeleton

```c
/* Single instance */
esp_err_t <name>_init(const <name>_config_t *cfg);   /* ESP_ERR_INVALID_STATE if already initialized */
esp_err_t <name>_deinit(void);                       /* safe to call when never initialized */

/* Multi instance */
typedef struct <name>_obj *<name>_handle_t;
esp_err_t <name>_create(const <name>_config_t *cfg, <name>_handle_t *out_handle);
esp_err_t <name>_delete(<name>_handle_t handle);
```

- The config struct has a `<NAME>_CONFIG_DEFAULT()` macro that reads values from Kconfig.
- Registered callbacks come with a `void *user_ctx`.
- If there's a task: create it in init, shut it down cleanly in deinit (event bit + wait for the
  task to delete itself, with a timeout).

Follow the [C/ESP-IDF rules](../../instructions/c-esp-idf.instructions.md) and the
[build rules](../../instructions/cmake-kconfig.instructions.md). After creating it: add the
component to section 3 "Component list" in `docs/architecture.md`, run `idf.py build`, and report
the result.
