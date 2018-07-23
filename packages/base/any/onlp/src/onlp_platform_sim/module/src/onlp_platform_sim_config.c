/**************************************************************************//**
 *
 *
 *
 *****************************************************************************/
#include <onlp_platform_sim/onlp_platform_sim_config.h>

/* <auto.start.cdefs(ONLP_PLATFORM_SIM_CONFIG_HEADER).source> */
#define __onlp_platform_sim_config_STRINGIFY_NAME(_x) #_x
#define __onlp_platform_sim_config_STRINGIFY_VALUE(_x) __onlp_platform_sim_config_STRINGIFY_NAME(_x)
onlp_platform_sim_config_settings_t onlp_platform_sim_config_settings[] =
{
#ifdef ONLP_PLATFORM_SIM_CONFIG_INCLUDE_LOGGING
    { __onlp_platform_sim_config_STRINGIFY_NAME(ONLP_PLATFORM_SIM_CONFIG_INCLUDE_LOGGING), __onlp_platform_sim_config_STRINGIFY_VALUE(ONLP_PLATFORM_SIM_CONFIG_INCLUDE_LOGGING) },
#else
{ ONLP_PLATFORM_SIM_CONFIG_INCLUDE_LOGGING(__onlp_platform_sim_config_STRINGIFY_NAME), "__undefined__" },
#endif
#ifdef ONLP_PLATFORM_SIM_CONFIG_LOG_OPTIONS_DEFAULT
    { __onlp_platform_sim_config_STRINGIFY_NAME(ONLP_PLATFORM_SIM_CONFIG_LOG_OPTIONS_DEFAULT), __onlp_platform_sim_config_STRINGIFY_VALUE(ONLP_PLATFORM_SIM_CONFIG_LOG_OPTIONS_DEFAULT) },
#else
{ ONLP_PLATFORM_SIM_CONFIG_LOG_OPTIONS_DEFAULT(__onlp_platform_sim_config_STRINGIFY_NAME), "__undefined__" },
#endif
#ifdef ONLP_PLATFORM_SIM_CONFIG_LOG_BITS_DEFAULT
    { __onlp_platform_sim_config_STRINGIFY_NAME(ONLP_PLATFORM_SIM_CONFIG_LOG_BITS_DEFAULT), __onlp_platform_sim_config_STRINGIFY_VALUE(ONLP_PLATFORM_SIM_CONFIG_LOG_BITS_DEFAULT) },
#else
{ ONLP_PLATFORM_SIM_CONFIG_LOG_BITS_DEFAULT(__onlp_platform_sim_config_STRINGIFY_NAME), "__undefined__" },
#endif
#ifdef ONLP_PLATFORM_SIM_CONFIG_LOG_CUSTOM_BITS_DEFAULT
    { __onlp_platform_sim_config_STRINGIFY_NAME(ONLP_PLATFORM_SIM_CONFIG_LOG_CUSTOM_BITS_DEFAULT), __onlp_platform_sim_config_STRINGIFY_VALUE(ONLP_PLATFORM_SIM_CONFIG_LOG_CUSTOM_BITS_DEFAULT) },
#else
{ ONLP_PLATFORM_SIM_CONFIG_LOG_CUSTOM_BITS_DEFAULT(__onlp_platform_sim_config_STRINGIFY_NAME), "__undefined__" },
#endif
#ifdef ONLP_PLATFORM_SIM_CONFIG_PORTING_STDLIB
    { __onlp_platform_sim_config_STRINGIFY_NAME(ONLP_PLATFORM_SIM_CONFIG_PORTING_STDLIB), __onlp_platform_sim_config_STRINGIFY_VALUE(ONLP_PLATFORM_SIM_CONFIG_PORTING_STDLIB) },
#else
{ ONLP_PLATFORM_SIM_CONFIG_PORTING_STDLIB(__onlp_platform_sim_config_STRINGIFY_NAME), "__undefined__" },
#endif
#ifdef ONLP_PLATFORM_SIM_CONFIG_PORTING_INCLUDE_STDLIB_HEADERS
    { __onlp_platform_sim_config_STRINGIFY_NAME(ONLP_PLATFORM_SIM_CONFIG_PORTING_INCLUDE_STDLIB_HEADERS), __onlp_platform_sim_config_STRINGIFY_VALUE(ONLP_PLATFORM_SIM_CONFIG_PORTING_INCLUDE_STDLIB_HEADERS) },
#else
{ ONLP_PLATFORM_SIM_CONFIG_PORTING_INCLUDE_STDLIB_HEADERS(__onlp_platform_sim_config_STRINGIFY_NAME), "__undefined__" },
#endif
#ifdef ONLP_PLATFORM_SIM_CONFIG_INCLUDE_UCLI
    { __onlp_platform_sim_config_STRINGIFY_NAME(ONLP_PLATFORM_SIM_CONFIG_INCLUDE_UCLI), __onlp_platform_sim_config_STRINGIFY_VALUE(ONLP_PLATFORM_SIM_CONFIG_INCLUDE_UCLI) },
#else
{ ONLP_PLATFORM_SIM_CONFIG_INCLUDE_UCLI(__onlp_platform_sim_config_STRINGIFY_NAME), "__undefined__" },
#endif
    { NULL, NULL }
};
#undef __onlp_platform_sim_config_STRINGIFY_VALUE
#undef __onlp_platform_sim_config_STRINGIFY_NAME

const char*
onlp_platform_sim_config_lookup(const char* setting)
{
    int i;
    for(i = 0; onlp_platform_sim_config_settings[i].name; i++) {
        if(strcmp(onlp_platform_sim_config_settings[i].name, setting)) {
            return onlp_platform_sim_config_settings[i].value;
        }
    }
    return NULL;
}

int
onlp_platform_sim_config_show(struct aim_pvs_s* pvs)
{
    int i;
    for(i = 0; onlp_platform_sim_config_settings[i].name; i++) {
        aim_printf(pvs, "%s = %s\n", onlp_platform_sim_config_settings[i].name, onlp_platform_sim_config_settings[i].value);
    }
    return i;
}

/* <auto.end.cdefs(ONLP_PLATFORM_SIM_CONFIG_HEADER).source> */

