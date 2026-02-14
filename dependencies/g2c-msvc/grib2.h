/** @file
 * @brief Header file for NCEPLIBS-g2c library (configured for F4Wx: no PNG/JPEG2000/AEC).
 */

#ifndef _grib2_H
#define _grib2_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
#include <stdio.h>
#include <string.h>

#if defined(_MSC_VER)
#include <basetsd.h>
typedef SSIZE_T ssize_t;
#else
#include <sys/types.h>
#endif

#define G2C_VERSION "2.1.0"

/* Constants required by grib2_int.h (from grib2.h.in). */
#define G2C_MAX_FILES 3
#define G2C_MAX_NAME 1024
#define G2C_SECTION0_BYTES 16
#define G2C_SECTION1_BYTES 21
#define G2C_SECTION0_LEN 3
#define G2C_SECTION1_LEN 13
#define G2C_SECTION0_ARRAY_LEN 3
#define G2C_SECTION1_ARRAY_LEN 13
#define G2C_MAX_GRIB_DESC_LEN 512
#define G2C_MAX_GRIB_STATUS_LEN 40
#define G2C_MAX_GRIB_CODE_LEN 20
#define G2C_MAX_GRIB_TITLE_LEN 200
#define G2C_MAX_NOAA_ABBREV_LEN 8
#define G2C_MAX_GDS_TEMPLATE 38
#define G2C_MAX_GDS_TEMPLATE_MAPLEN 28
#define G2C_MAX_PDS_TEMPLATE 102
#define G2C_MAX_PDS_TEMPLATE_MAPLEN 50
#define G2C_MAX_DRS_TEMPLATE 11
#define G2C_MAX_DRS_TEMPLATE_MAPLEN 18

/** Long integer type. */
typedef int64_t g2int;

/** Unsigned long integer type. */
typedef uint64_t g2intu;

/** Float type. */
typedef float g2float;

/**
 * Struct for GRIB2 field.
 */
struct gribfield
{
    g2int version;
    g2int discipline;
    g2int *idsect;
    g2int idsectlen;
    unsigned char *local;
    g2int locallen;
    g2int ifldnum;
    g2int griddef;
    g2int ngrdpts;
    g2int numoct_opt;
    g2int interp_opt;
    g2int num_opt;
    g2int *list_opt;
    g2int igdtnum;
    g2int igdtlen;
    g2int *igdtmpl;
    g2int ipdtnum;
    g2int ipdtlen;
    g2int *ipdtmpl;
    g2int num_coord;
    float *coord_list;
    g2int ndpts;
    g2int idrtnum;
    g2int idrtlen;
    g2int *idrtmpl;
    g2int unpacked;
    g2int expanded;
    g2int ibmap;
    g2int *bmap;
    float *fld;
};

typedef struct gribfield gribfield;

/* Prototypes for unpacking sections API */
g2int g2_unpack1(unsigned char *cgrib, g2int *iofst, g2int **ids, g2int *idslen);
g2int g2_unpack2(unsigned char *cgrib, g2int *iofst, g2int *lencsec2, unsigned char **csec2);
g2int g2_unpack3(unsigned char *cgrib, g2int *iofst, g2int **igds, g2int **igdstmpl, g2int *mapgridlen, g2int **ideflist, g2int *idefnum);
g2int g2_unpack4(unsigned char *cgrib, g2int *iofst, g2int *ipdsnum, g2int **ipdstmpl, g2int *mappdslen, float **coordlist, g2int *numcoord);
g2int g2_unpack5(unsigned char *cgrib, g2int *iofst, g2int *ndpts, g2int *idrsnum, g2int **idrstmpl, g2int *mapdrslen);
g2int g2_unpack6(unsigned char *cgrib, g2int *iofst, g2int ngpts, g2int *ibmap, g2int **bmap);
g2int g2_unpack7(unsigned char *cgrib, g2int *iofst, g2int igdsnum, g2int *igdstmpl, g2int idrsnum, g2int *idrstmpl, g2int ndpts, float **fld);

/* Prototypes for unpacking API */
void seekgb(FILE *lugb, g2int iseek, g2int mseek, g2int *lskip, g2int *lgrib);
g2int g2_info(unsigned char *cgrib, g2int *listsec0, g2int *listsec1, g2int *numfields, g2int *numlocal);
g2int g2_getfld(unsigned char *cgrib, g2int ifldnum, g2int unpack, g2int expand, gribfield **gfld);
void g2_free(gribfield *gfld);

/* Prototypes for packing API */
g2int g2_create(unsigned char *cgrib, g2int *listsec0, g2int *listsec1);
g2int g2_addlocal(unsigned char *cgrib, unsigned char *csec2, g2int lcsec2);
g2int g2_addgrid(unsigned char *cgrib, g2int *igds, g2int *igdstmpl, g2int *ideflist, g2int idefnum);
g2int g2_addfield(unsigned char *cgrib, g2int ipdsnum, g2int *ipdstmpl, float *coordlist, g2int numcoord, g2int idrsnum, g2int *idrstmpl, float *fld, g2int ngrdpts, g2int ibmap, g2int *bmap);
g2int g2_gribend(unsigned char *cgrib);

/* Error codes (G2_* and G2C_* from grib2.h.in) */
#define G2_NO_ERROR 0
#define G2_CREATE_GRIB_VERSION -1
#define G2_INFO_NO_GRIB 1
#define G2_INFO_GRIB_VERSION 2
#define G2_INFO_NO_SEC1 3
#define G2_INFO_WRONG_END 4
#define G2_INFO_BAD_END 5
#define G2_INFO_INVAL_SEC 6
#define G2_GETFLD_NO_GRIB 1
#define G2_GETFLD_GRIB_VERSION 2
#define G2_GETFLD_INVAL 3
#define G2_GETFLD_WRONG_END 4
#define G2_GETFLD_WRONG_NFLDS 6
#define G2_GETFLD_BAD_END 7
#define G2_GETFLD_INVAL_SEC 8
#define G2_GETFLD_NO_DRT 9
#define G2_GETFLD_BAD_SEC3 10
#define G2_GETFLD_BAD_SEC4 11
#define G2_GETFLD_BAD_SEC5 12
#define G2_GETFLD_BAD_SEC6 13
#define G2_GETFLD_BAD_SEC7 14
#define G2_GETFLD_BAD_SEC1 15
#define G2_GETFLD_BAD_SEC2 16
#define G2_GETFLD_NO_BITMAP 17
#define G2_GRIBEND_MSG_INIT -1
#define G2_BAD_SEC -4
#define G2_UNPACK_BAD_SEC 2
#define G2_UNPACK_NO_MEM 6
#define G2_UNPACK3_BAD_GDT 5
#define G2_UNPACK4_BAD_PDT 5
#define G2_UNPACK5_BAD_DRT 7
#define G2_UNPACK6_BAD_BITMAP 4
#define G2_UNPACK7_CORRUPT_SEC 7
#define G2_UNPACK7_WRONG_GDT 5
#define G2_UNPACK7_BAD_DRT 4
#define G2_ADD_MSG_INIT -1
#define G2_ADD_MSG_COMPLETE -2
#define G2_BAD_SEC_COUNTS -3
#define G2_ADDFIELD_BAD_PDT -5
#define G2_ADDFIELD_BAD_GDS -6
#define G2_ADDFIELD_BAD_DRT -7
#define G2_ADDFIELD_BAD_BITMAP -8
#define G2_ADDFIELD_BAD_GDT -9
#define G2_ADDFIELD_ERR -10
#define G2_ADDGRID_BAD_GDT -5
#define G2_JPCUNPACK_MEM 1
#define G2_SPECUNPACK_TYPE -3
#define G2C_NOERROR 0
#define G2C_ERROR 1
#define G2C_ENOTGRIB (-50)
#define G2C_EMSGCOMPLETE (-51)
#define G2C_ENAMETOOLONG (-52)
#define G2C_EINVAL (-53)
#define G2C_EFILE (-54)
#define G2C_EBADID (-55)
#define G2C_ETOOMANYFILES (-56)
#define G2C_ENOMEM (-57)
#define G2C_EMSG (-58)
#define G2C_ENOMSG (-59)
#define G2C_EXML (-60)
#define G2C_ENOTFOUND (-61)
#define G2C_ENOTGRIB2 (-62)
#define G2C_ENOSECTION (-63)
#define G2C_ENOEND (-64)
#define G2C_EBADEND (-65)
#define G2C_EBADSECTION (-66)
#define G2C_EJPEG (-67)
#define G2C_EPNG (-68)
#define G2C_ENOTEMPLATE (-69)
#define G2C_EBADTEMPLATE (-70)
#define G2C_ENOPARAM (-71)
#define G2C_ENOPRODUCT (-72)
#define G2C_EBADTYPE (-73)
#define G2C_EAEC (-74)
#define G2C_ECSV (-75)

#ifdef __cplusplus
}
#endif

#endif /* _grib2_H */
