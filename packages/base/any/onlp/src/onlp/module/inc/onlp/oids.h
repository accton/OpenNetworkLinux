/************************************************************
 * <bsn.cl fy=2014 v=onl>
 *
 *        Copyright 2014, 2015 Big Switch Networks, Inc.
 *
 * Licensed under the Eclipse Public License, Version 1.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *        http://www.eclipse.org/legal/epl-v10.html
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the
 * License.
 *
 * </bsn.cl>
 ************************************************************
 *
 * ONLP Platform Object Identifiers.
 *
 ***********************************************************/
#ifndef __ONLP_OID_H__
#define __ONLP_OID_H__

#include <onlp/onlp_config.h>

#include <stdint.h>
#include <AIM/aim_pvs.h>
#include <IOF/iof.h>
#include <BigList/biglist.h>
#include <cjson/cJSON.h>

/**
 * System peripherals are identified by a 32bit OID.
 *
 * The First byte is the object-class identifier:
 *    Thermal sensor object
 *    Fan object
 *    PSU object
 *    LED object
 *    MODULE object
 *    etc..
 * The remaining bytes are the object id.
 */

typedef uint32_t onlp_oid_t;

/* <auto.start.enum(tag:oid).define> */
/** onlp_oid_dump */
typedef enum onlp_oid_dump_e {
    ONLP_OID_DUMP_RECURSE,
    ONLP_OID_DUMP_EVEN_IF_ABSENT,
    ONLP_OID_DUMP_LAST = ONLP_OID_DUMP_EVEN_IF_ABSENT,
    ONLP_OID_DUMP_COUNT,
    ONLP_OID_DUMP_INVALID = -1,
} onlp_oid_dump_t;

/** onlp_oid_format */
typedef enum onlp_oid_format_e {
    ONLP_OID_FORMAT_JSON,
    ONLP_OID_FORMAT_YAML,
    ONLP_OID_FORMAT_USER,
    ONLP_OID_FORMAT_DEBUG,
    ONLP_OID_FORMAT_LAST = ONLP_OID_FORMAT_DEBUG,
    ONLP_OID_FORMAT_COUNT,
    ONLP_OID_FORMAT_INVALID = -1,
} onlp_oid_format_t;

/** onlp_oid_format_flags */
typedef enum onlp_oid_format_flags_e {
    ONLP_OID_FORMAT_FLAGS_RECURSIVE = (1 << 0),
    ONLP_OID_FORMAT_FLAGS_MISSING = (1 << 1),
} onlp_oid_format_flags_t;

/** onlp_oid_json_flag */
typedef enum onlp_oid_json_flag_e {
    ONLP_OID_JSON_FLAG_RECURSIVE = (1 << 0),
    ONLP_OID_JSON_FLAG_UNSUPPORTED_FIELDS = (1 << 1),
} onlp_oid_json_flag_t;

/** onlp_oid_show */
typedef enum onlp_oid_show_e {
    ONLP_OID_SHOW_RECURSE = (1 << 0),
    ONLP_OID_SHOW_EXTENDED = (1 << 1),
    ONLP_OID_SHOW_YAML = (1 << 2),
} onlp_oid_show_t;

/** onlp_oid_status_flag */
typedef enum onlp_oid_status_flag_e {
    ONLP_OID_STATUS_FLAG_PRESENT = (1 << 0),
    ONLP_OID_STATUS_FLAG_FAILED = (1 << 1),
    ONLP_OID_STATUS_FLAG_OPERATIONAL = (1 << 2),
    ONLP_OID_STATUS_FLAG_UNPLUGGED = (1 << 3),
} onlp_oid_status_flag_t;

/** onlp_oid_type */
typedef enum onlp_oid_type_e {
    ONLP_OID_TYPE_CHASSIS = 1,
    ONLP_OID_TYPE_MODULE = 2,
    ONLP_OID_TYPE_THERMAL = 3,
    ONLP_OID_TYPE_FAN = 4,
    ONLP_OID_TYPE_PSU = 5,
    ONLP_OID_TYPE_LED = 6,
    ONLP_OID_TYPE_SFP = 7,
    ONLP_OID_TYPE_GENERIC = 8,
} onlp_oid_type_t;

/** onlp_oid_type_flag */
typedef enum onlp_oid_type_flag_e {
    ONLP_OID_TYPE_FLAG_CHASSIS = (1 << 1),
    ONLP_OID_TYPE_FLAG_MODULE = (1 << 2),
    ONLP_OID_TYPE_FLAG_THERMAL = (1 << 3),
    ONLP_OID_TYPE_FLAG_FAN = (1 << 4),
    ONLP_OID_TYPE_FLAG_PSU = (1 << 5),
    ONLP_OID_TYPE_FLAG_LED = (1 << 6),
    ONLP_OID_TYPE_FLAG_SFP = (1 << 7),
    ONLP_OID_TYPE_FLAG_GENERIC = (1 << 8),
} onlp_oid_type_flag_t;
/* <auto.end.enum(tag:oid).define> */

/**
 * Represents a set of oid_type_flags.
 */
typedef uint32_t onlp_oid_type_flags_t;

/**
 * Represents a set of oid_status_flags;
 */
typedef uint32_t onlp_oid_status_flags_t;

/**
 * Get the or set the type of an OID
 */
#define ONLP_OID_TYPE_GET(_id) ( ( (_id) >> 24) )
#define ONLP_OID_TYPE_CREATE(_type, _id) ( ( (_type) << 24) | (_id))
#define ONLP_OID_IS_TYPE(_type,_id) (ONLP_OID_TYPE_GET((_id)) == _type)
#define ONLP_OID_ID_GET(_id) (_id & 0xFFFFFF)
#define ONLP_OID_TYPE_VALIDATE(_type, _id)      \
    do {                                        \
        if(!ONLP_OID_IS_TYPE(_type, _id)) {     \
            return ONLP_STATUS_E_PARAM;         \
        }                                       \
    } while(0)

#define ONLP_OID_TYPE_VALIDATE_NR(_type, _id)   \
    do {                                        \
        if(!ONLP_OID_IS_TYPE(_type, _id)) {     \
            return;                             \
        }                                       \
    } while(0)

#define ONLP_OID_IS_TYPE_FLAGS(_flags, _id) ((_flags & (1 << ONLP_OID_TYPE_GET(_id))))
#define ONLP_OID_IS_TYPE_FLAGSZ(_flags, _id) ((_flags == 0) || ONLP_OID_IS_TYPE_FLAGS(_flags, _id))

#define ONLP_CHASSIS_ID_CREATE(_id) ONLP_OID_TYPE_CREATE(ONLP_OID_TYPE_CHASSIS, _id)
#define ONLP_THERMAL_ID_CREATE(_id) ONLP_OID_TYPE_CREATE(ONLP_OID_TYPE_THERMAL, _id)
#define ONLP_FAN_ID_CREATE(_id)     ONLP_OID_TYPE_CREATE(ONLP_OID_TYPE_FAN, _id)
#define ONLP_PSU_ID_CREATE(_id)     ONLP_OID_TYPE_CREATE(ONLP_OID_TYPE_PSU, _id)
#define ONLP_LED_ID_CREATE(_id)     ONLP_OID_TYPE_CREATE(ONLP_OID_TYPE_LED, _id)
#define ONLP_SFP_ID_CREATE(_id)     ONLP_OID_TYPE_CREATE(ONLP_OID_TYPE_SFP, _id)
#define ONLP_MODULE_ID_CREATE(_id)  ONLP_OID_TYPE_CREATE(ONLP_OID_TYPE_MODULE, _id)

#define ONLP_OID_IS_CHASSIS(_id) ONLP_OID_IS_TYPE(ONLP_OID_TYPE_CHASSIS, _id)
#define ONLP_OID_CHASSIS_VALIDATE(_id) ONLP_OID_TYPE_VALIDATE(ONLP_OID_TYPE_CHASSIS, _id)
#define ONLP_OID_CHASSIS_VALIDATE_NR(_id) ONLP_OID_TYPE_VALIDATE_NR(ONLP_OID_TYPE_CHASSIS, _id)

#define ONLP_OID_IS_THERMAL(_id) ONLP_OID_IS_TYPE(ONLP_OID_TYPE_THERMAL, _id)
#define ONLP_OID_THERMAL_VALIDATE(_id) ONLP_OID_TYPE_VALIDATE(ONLP_OID_TYPE_THERMAL, _id)
#define ONLP_OID_THERMAL_VALIDATE_NR(_id) ONLP_OID_TYPE_VALIDATE_NR(ONLP_OID_TYPE_THERMAL, _id)

#define ONLP_OID_IS_FAN(_id)     ONLP_OID_IS_TYPE(ONLP_OID_TYPE_FAN, _id)
#define ONLP_OID_FAN_VALIDATE(_id) ONLP_OID_TYPE_VALIDATE(ONLP_OID_TYPE_FAN, _id)
#define ONLP_OID_FAN_VALIDATE_NR(_id) ONLP_OID_TYPE_VALIDATE_NR(ONLP_OID_TYPE_FAN, _id)

#define ONLP_OID_IS_PSU(_id)     ONLP_OID_IS_TYPE(ONLP_OID_TYPE_PSU, _id)
#define ONLP_OID_PSU_VALIDATE(_id) ONLP_OID_TYPE_VALIDATE(ONLP_OID_TYPE_PSU, _id)
#define ONLP_OID_PSU_VALIDATE_NR(_id) ONLP_OID_TYPE_VALIDATE_NR(ONLP_OID_TYPE_PSU, _id)

#define ONLP_OID_IS_LED(_id)     ONLP_OID_IS_TYPE(ONLP_OID_TYPE_LED, _id)
#define ONLP_OID_LED_VALIDATE(_id) ONLP_OID_TYPE_VALIDATE(ONLP_OID_TYPE_LED, _id)
#define ONLP_OID_LED_VALIDATE_NR(_id) ONLP_OID_TYPE_VALIDATE_NR(ONLP_OID_TYPE_LED, _id)

#define ONLP_OID_IS_SFP(_id)     ONLP_OID_IS_TYPE(ONLP_OID_TYPE_SFP, _id)
#define ONLP_OID_SFP_VALIDATE(_id) ONLP_OID_TYPE_VALIDATE(ONLP_OID_TYPE_SFP, _id)
#define ONLP_OID_SFP_VALIDATE_NR(_id) ONLP_OID_TYPE_VALIDATE_NR(ONLP_OID_TYPE_SFP, _id)

#define ONLP_OID_IS_MODULE(_id)  ONLP_OID_IS_TYPE(ONLP_OID_TYPE_MODULE, _id)
#define ONLP_OID_MODULE_VALIDATE(_id) ONLP_OID_TYPE_VALIDATE(ONLP_OID_TYPE_MODULE, _id)
#define ONLP_OID_MODULE_VALIDATE_NR(_id) ONLP_OID_TYPE_VALIDATE_NR(ONLP_OID_TYPE_MODULE, _id)

#define ONLP_OID_IS_GENERIC(_id) ONLP_OID_IS_TYPE(ONLP_OID_TYPE_GENERIC, _id)
#define ONLP_OID_GENERIC_VALIDATE(_id) ONLP_OID_TYPE_VALIDATE(ONLP_OID_TYPE_GENERIC, _id)
#define ONLP_OID_GENERIC_VALIDATE_NR(_id) ONLP_OID_TYPE_VALIDATE_NR(ONLP_OID_TYPE_GENERIC, _id)


/**
 * There is only one Chassis OID. This value should be used.
 */
#define ONLP_OID_CHASSIS ONLP_OID_TYPE_CREATE(ONLP_OID_TYPE_CHASSIS, 1)

/**
 * All OIDs have user-level description strings:
 */
#define ONLP_OID_DESC_SIZE 128

typedef char onlp_oid_desc_t[ONLP_OID_DESC_SIZE];

/* fixme */
#define ONLP_OID_TABLE_SIZE 256
typedef onlp_oid_t onlp_oid_table_t[ONLP_OID_TABLE_SIZE];
#define ONLP_OID_TABLE_SIZE_BYTES (sizeof(onlp_oid_t)*ONLP_OID_TABLE_SIZE)
#define ONLP_OID_TABLE_COPY(_dst, _src) memcpy(_dst, _src, ONLP_OID_TABLE_SIZE_BYTES)
#define ONLP_OID_TABLE_CLEAR(_table) memset(_table, 0, ONLP_OID_TABLE_SIZE_BYTES)


/**
 * This macro can be used to construct your OID hdr tables
 */
#define ONLP_OID_THERMAL_ENTRY(_id, _desc, _parent_type, _parent_id) \
    { ONLP_THERMAL_ID_CREATE(_id), _desc, ONLP_OID_TYPE_CREATE(_parent_type, _parent_id) }


/**
 * All OID objects contain this header as the first member.
 */
typedef struct onlp_oid_hdr_s {
    /** The OID */
    onlp_oid_t id;
    /** The description of this object. */
    onlp_oid_desc_t description;
    /** The parent OID of this object. */
    onlp_oid_t poid;
    /** The children of this OID */
    onlp_oid_table_t coids;

    /** The current status (if applicable) */
    onlp_oid_status_flags_t status;

} onlp_oid_hdr_t;


/**
 * @brief Get the OID header for a given OID.
 * @param oid The oid
 * @param[out] hdr Receives the header
 */
int onlp_oid_hdr_get(onlp_oid_t oid, onlp_oid_hdr_t* hdr);

/**
 * @brief Get the information structure for a given OID.
 * @param oid The oid
 * @param[out] info Receives a pointer to the information structure.
 */
int onlp_oid_info_get(onlp_oid_t oid, onlp_oid_hdr_t** info);

int onlp_oid_format(onlp_oid_t oid, onlp_oid_format_t format,
                    aim_pvs_t* pvs, uint32_t flags);

int onlp_oid_info_format(onlp_oid_hdr_t* info, onlp_oid_format_t format,
                         aim_pvs_t* pvs, uint32_t flags);

int onlp_oid_table_format(onlp_oid_table_t table, onlp_oid_format_t format,
                          aim_pvs_t* pvs, uint32_t flags);

/**
 * Iterator
 */
typedef int (*onlp_oid_iterate_f)(onlp_oid_t oid, void* cookie);

/**
 * @brief Iterate over all platform OIDs.
 * @param oid The root OID.
 * @param type The OID type filter (optional)
 * @param itf The iterator function.
 * @param cookie The cookie.
 */
int onlp_oid_iterate(onlp_oid_t oid, onlp_oid_type_flags_t types,
                     onlp_oid_iterate_f itf, void* cookie);


int onlp_oid_info_get_all(onlp_oid_t root, onlp_oid_type_flags_t types,
                          uint32_t flags, biglist_t** list);

int onlp_oid_info_format_all(onlp_oid_t root, onlp_oid_type_flags_t types,
                             uint32_t get_flags,
                             onlp_oid_format_t format, aim_pvs_t* pvs,
                             uint32_t format_flags);

int onlp_oid_hdr_get_all(onlp_oid_t root, onlp_oid_type_flags_t types,
                         uint32_t flags, biglist_t** list);

int onlp_oid_hdr_format_all(onlp_oid_t root, onlp_oid_type_flags_t types,
                            uint32_t get_flags,
                            onlp_oid_format_t format, aim_pvs_t* pvs,
                            uint32_t format_flags);

int onlp_oid_get_all_free(biglist_t* list);

/**
 * Manipulating OID Status Flags
 */
#define ONLP_OID_STATUS_FLAGS_GET(_ptr)         \
    (((onlp_oid_hdr_t*)_ptr)->status)

#define ONLP_OID_STATUS_FLAG_GET_VALUE(_ptr, _name)                     \
    AIM_FLAG_GET_VALUE(ONLP_OID_STATUS_FLAGS_GET(_ptr), ONLP_OID_STATUS_FLAG_##_name)

#define ONLP_OID_STATUS_FLAG_SET_VALUE(_ptr, _name) \
    AIM_FLAG_SET_VALUE(ONLP_OID_STATUS_FLAGS_GET(_ptr), ONLP_OID_STATUS_FLAG_##_name)

#define ONLP_OID_STATUS_FLAG_SET(_ptr, _name)                           \
    AIM_FLAG_SET(ONLP_OID_STATUS_FLAGS_GET(_ptr), ONLP_OID_STATUS_FLAG_##_name)

#define ONLP_OID_STATUS_FLAG_CLR(_ptr, _name) \
    AIM_FLAG_CLR(ONLP_OID_STATUS_FLAGS_GET(_ptr), ONLP_OID_STATUS_FLAG_##_name)

#define ONLP_OID_STATUS_FLAG_IS_SET(_ptr, _name)                        \
    AIM_FLAG_IS_SET(ONLP_OID_STATUS_FLAGS_GET(_ptr), ONLP_OID_STATUS_FLAG_##_name)

#define ONLP_OID_STATUS_FLAG_NOT_SET(_ptr, _name)                       \
    AIM_FLAG_NOT_SET(ONLP_OID_STATUS_FLAGS_GET(_ptr), ONLP_OID_STATUS_FLAG_##_name)

/**
 * Common Shorthands
 */
#define ONLP_OID_PRESENT(_ptr) \
    ONLP_OID_STATUS_FLAG_IS_SET(_ptr, PRESENT)

#define ONLP_OID_FAILED(_ptr) \
    ONLP_OID_STATUS_FLAG_IS_SET(_ptr, FAILED)

/**
 * @brief Iterate over all OIDS in the given table that match the given expression.
 * @param _table The OID table
 * @param _oidp    OID pointer iterator
 * @param _expr    OID Expression which must be true
 */
#define ONLP_OID_TABLE_ITER_EXPR(_table, _oidp, _expr)                 \
    for(_oidp = _table; _oidp < (_table+ONLP_OID_TABLE_SIZE); _oidp++) \
        if( (*_oidp) && (_expr) )

/**
 * @brief Iterate over all OIDs in the given table.
 * @param _table The OID table
 * @param _oidp  OID pointer iterator
 */
#define ONLP_OID_TABLE_ITER(_table, _oidp) ONLP_OID_TABLE_ITER_EXPR(_table, _oidp, 1)

/**
 * @brief Iterate over all OIDs in the given table of the given type.
 * @param _table The OID table
 * @param _oidp  OID pointer iteration.
 * @param _type  The OID Type
 */

#define ONLP_OID_TABLE_ITER_TYPE(_table, _oidp, _type)                  \
    ONLP_OID_TABLE_ITER_EXPR(_table, _oidp, ONLP_OID_IS_TYPE(ONLP_OID_TYPE_##_type, *_oidp))


/**
 * @brief Return whether an OID is present or not.
 */
int onlp_oid_is_present(onlp_oid_t* oid);


/**
 * @brief OID -> String Representation
 */
int onlp_oid_to_str(onlp_oid_t oid, char* rstr);
int onlp_oid_to_user_str(onlp_oid_t oid, char* rstr);

/**
 * @brief String Represenation -> OID
 */
int onlp_oid_from_str(char* str, onlp_oid_t* roid);

/**
 * @brief OID Table -> JSON
 * @param table The table.
 * @param[out] Receives the JSON array object.
 */
int onlp_oid_table_to_json(onlp_oid_table_t table, cJSON** cjp);

/**
 * @brief JSON -> OID Table
 * @param cj The CJSON array object.
 * @param[out] table The table to populate.
 */
int onlp_oid_table_from_json(cJSON* cj, onlp_oid_table_t table);

/**
 * @brief OID Header -> JSON
 * @param hdr The header
 * @param[out] cj Receives the JSON representation.
 */
int onlp_oid_hdr_to_json(onlp_oid_hdr_t* hdr, cJSON** cj, uint32_t flags);

/**
 * @brief JSON -> OID Header
 * @param cj The source json
 * @param[out] hdr receives the header.
 */
int onlp_oid_hdr_from_json(cJSON* cj, onlp_oid_hdr_t* hdr);

int onlp_oid_info_to_json(onlp_oid_hdr_t* info, cJSON** cj, uint32_t flags);
/**
 * @brief OID -> JSON
 */
int onlp_oid_to_user_json(onlp_oid_t oid, cJSON** rv, uint32_t flags);
int onlp_oid_to_json(onlp_oid_t oid, cJSON** rv, uint32_t flags);
int onlp_oid_from_json(cJSON* cj, onlp_oid_hdr_t** hdr, biglist_t** all, uint32_t flags);

/**
 * Verify OID <-> JSON conversions for the given OID (testing).
 */
int onlp_oid_json_verify(onlp_oid_t oid);

/******************************************************************************
 *
 * Enumeration Support Definitions.
 *
 * Please do not add additional code beyond this point.
 *
 *****************************************************************************/
/* <auto.start.enum(tag:oid).supportheader> */
/** Strings macro. */
#define ONLP_OID_DUMP_STRINGS \
{\
    "RECURSE", \
    "EVEN_IF_ABSENT", \
}
/** Enum names. */
const char* onlp_oid_dump_name(onlp_oid_dump_t e);

/** Enum values. */
int onlp_oid_dump_value(const char* str, onlp_oid_dump_t* e, int substr);

/** Enum descriptions. */
const char* onlp_oid_dump_desc(onlp_oid_dump_t e);

/** validator */
#define ONLP_OID_DUMP_VALID(_e) \
    ( (0 <= (_e)) && ((_e) <= ONLP_OID_DUMP_EVEN_IF_ABSENT))

/** onlp_oid_dump_map table. */
extern aim_map_si_t onlp_oid_dump_map[];
/** onlp_oid_dump_desc_map table. */
extern aim_map_si_t onlp_oid_dump_desc_map[];

/** Strings macro. */
#define ONLP_OID_FORMAT_STRINGS \
{\
    "JSON", \
    "YAML", \
    "USER", \
    "DEBUG", \
}
/** Enum names. */
const char* onlp_oid_format_name(onlp_oid_format_t e);

/** Enum values. */
int onlp_oid_format_value(const char* str, onlp_oid_format_t* e, int substr);

/** Enum descriptions. */
const char* onlp_oid_format_desc(onlp_oid_format_t e);

/** validator */
#define ONLP_OID_FORMAT_VALID(_e) \
    ( (0 <= (_e)) && ((_e) <= ONLP_OID_FORMAT_DEBUG))

/** onlp_oid_format_map table. */
extern aim_map_si_t onlp_oid_format_map[];
/** onlp_oid_format_desc_map table. */
extern aim_map_si_t onlp_oid_format_desc_map[];

/** Enum names. */
const char* onlp_oid_format_flags_name(onlp_oid_format_flags_t e);

/** Enum values. */
int onlp_oid_format_flags_value(const char* str, onlp_oid_format_flags_t* e, int substr);

/** Enum descriptions. */
const char* onlp_oid_format_flags_desc(onlp_oid_format_flags_t e);

/** Enum validator. */
int onlp_oid_format_flags_valid(onlp_oid_format_flags_t e);

/** validator */
#define ONLP_OID_FORMAT_FLAGS_VALID(_e) \
    (onlp_oid_format_flags_valid((_e)))

/** onlp_oid_format_flags_map table. */
extern aim_map_si_t onlp_oid_format_flags_map[];
/** onlp_oid_format_flags_desc_map table. */
extern aim_map_si_t onlp_oid_format_flags_desc_map[];

/** Enum names. */
const char* onlp_oid_json_flag_name(onlp_oid_json_flag_t e);

/** Enum values. */
int onlp_oid_json_flag_value(const char* str, onlp_oid_json_flag_t* e, int substr);

/** Enum descriptions. */
const char* onlp_oid_json_flag_desc(onlp_oid_json_flag_t e);

/** Enum validator. */
int onlp_oid_json_flag_valid(onlp_oid_json_flag_t e);

/** validator */
#define ONLP_OID_JSON_FLAG_VALID(_e) \
    (onlp_oid_json_flag_valid((_e)))

/** onlp_oid_json_flag_map table. */
extern aim_map_si_t onlp_oid_json_flag_map[];
/** onlp_oid_json_flag_desc_map table. */
extern aim_map_si_t onlp_oid_json_flag_desc_map[];

/** Enum names. */
const char* onlp_oid_show_name(onlp_oid_show_t e);

/** Enum values. */
int onlp_oid_show_value(const char* str, onlp_oid_show_t* e, int substr);

/** Enum descriptions. */
const char* onlp_oid_show_desc(onlp_oid_show_t e);

/** Enum validator. */
int onlp_oid_show_valid(onlp_oid_show_t e);

/** validator */
#define ONLP_OID_SHOW_VALID(_e) \
    (onlp_oid_show_valid((_e)))

/** onlp_oid_show_map table. */
extern aim_map_si_t onlp_oid_show_map[];
/** onlp_oid_show_desc_map table. */
extern aim_map_si_t onlp_oid_show_desc_map[];

/** Enum names. */
const char* onlp_oid_status_flag_name(onlp_oid_status_flag_t e);

/** Enum values. */
int onlp_oid_status_flag_value(const char* str, onlp_oid_status_flag_t* e, int substr);

/** Enum descriptions. */
const char* onlp_oid_status_flag_desc(onlp_oid_status_flag_t e);

/** Enum validator. */
int onlp_oid_status_flag_valid(onlp_oid_status_flag_t e);

/** validator */
#define ONLP_OID_STATUS_FLAG_VALID(_e) \
    (onlp_oid_status_flag_valid((_e)))

/** onlp_oid_status_flag_map table. */
extern aim_map_si_t onlp_oid_status_flag_map[];
/** onlp_oid_status_flag_desc_map table. */
extern aim_map_si_t onlp_oid_status_flag_desc_map[];

/** Enum names. */
const char* onlp_oid_type_name(onlp_oid_type_t e);

/** Enum values. */
int onlp_oid_type_value(const char* str, onlp_oid_type_t* e, int substr);

/** Enum descriptions. */
const char* onlp_oid_type_desc(onlp_oid_type_t e);

/** Enum validator. */
int onlp_oid_type_valid(onlp_oid_type_t e);

/** validator */
#define ONLP_OID_TYPE_VALID(_e) \
    (onlp_oid_type_valid((_e)))

/** onlp_oid_type_map table. */
extern aim_map_si_t onlp_oid_type_map[];
/** onlp_oid_type_desc_map table. */
extern aim_map_si_t onlp_oid_type_desc_map[];

/** Enum names. */
const char* onlp_oid_type_flag_name(onlp_oid_type_flag_t e);

/** Enum values. */
int onlp_oid_type_flag_value(const char* str, onlp_oid_type_flag_t* e, int substr);

/** Enum descriptions. */
const char* onlp_oid_type_flag_desc(onlp_oid_type_flag_t e);

/** Enum validator. */
int onlp_oid_type_flag_valid(onlp_oid_type_flag_t e);

/** validator */
#define ONLP_OID_TYPE_FLAG_VALID(_e) \
    (onlp_oid_type_flag_valid((_e)))

/** onlp_oid_type_flag_map table. */
extern aim_map_si_t onlp_oid_type_flag_map[];
/** onlp_oid_type_flag_desc_map table. */
extern aim_map_si_t onlp_oid_type_flag_desc_map[];
/* <auto.end.enum(tag:oid).supportheader> */

#endif /* __ONLP_OID_H__ */
