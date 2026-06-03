

/* this ALWAYS GENERATED file contains the proxy stub code */


 /* File created by MIDL compiler version 8.01.0628 */
/* at Tue Jan 19 11:14:07 2038
 */
/* Compiler settings for IConsoleHandoff.idl:
    Oicf, W1, Zp8, env=Win64 (32b run), target_arch=ARM64 8.01.0628 
    protocol : all , ms_ext, c_ext, robust
    error checks: allocation ref bounds_check enum stub_data 
    VC __declspec() decoration level: 
         __declspec(uuid()), __declspec(selectany), __declspec(novtable)
         DECLSPEC_UUID(), MIDL_INTERFACE()
*/
/* @@MIDL_FILE_HEADING(  ) */

#if defined(_M_ARM64)


#if _MSC_VER >= 1200
#pragma warning(push)
#endif

#pragma warning( disable: 4211 )  /* redefine extern to static */
#pragma warning( disable: 4232 )  /* dllimport identity*/
#pragma warning( disable: 4024 )  /* array to pointer mapping*/
#pragma warning( disable: 4152 )  /* function/data pointer conversion in expression */

#define USE_STUBLESS_PROXY


/* verify that the <rpcproxy.h> version is high enough to compile this file*/
#ifndef __REDQ_RPCPROXY_H_VERSION__
#define __REQUIRED_RPCPROXY_H_VERSION__ 475
#endif


#include "rpcproxy.h"
#include "ndr64types.h"
#ifndef __RPCPROXY_H_VERSION__
#error this stub requires an updated version of <rpcproxy.h>
#endif /* __RPCPROXY_H_VERSION__ */


#include "IConsoleHandoff.h"

#define TYPE_FORMAT_STRING_SIZE   53                                
#define PROC_FORMAT_STRING_SIZE   77                                
#define EXPR_FORMAT_STRING_SIZE   1                                 
#define TRANSMIT_AS_TABLE_SIZE    0            
#define WIRE_MARSHAL_TABLE_SIZE   0            

typedef struct _IConsoleHandoff_MIDL_TYPE_FORMAT_STRING
    {
    short          Pad;
    unsigned char  Format[ TYPE_FORMAT_STRING_SIZE ];
    } IConsoleHandoff_MIDL_TYPE_FORMAT_STRING;

typedef struct _IConsoleHandoff_MIDL_PROC_FORMAT_STRING
    {
    short          Pad;
    unsigned char  Format[ PROC_FORMAT_STRING_SIZE ];
    } IConsoleHandoff_MIDL_PROC_FORMAT_STRING;

typedef struct _IConsoleHandoff_MIDL_EXPR_FORMAT_STRING
    {
    long          Pad;
    unsigned char  Format[ EXPR_FORMAT_STRING_SIZE ];
    } IConsoleHandoff_MIDL_EXPR_FORMAT_STRING;


static const RPC_SYNTAX_IDENTIFIER  _RpcTransferSyntax_2_0 = 
{{0x8A885D04,0x1CEB,0x11C9,{0x9F,0xE8,0x08,0x00,0x2B,0x10,0x48,0x60}},{2,0}};

static const RPC_SYNTAX_IDENTIFIER  _NDR64_RpcTransferSyntax_1_0 = 
{{0x71710533,0xbeba,0x4937,{0x83,0x19,0xb5,0xdb,0xef,0x9c,0xcc,0x36}},{1,0}};

#if defined(_CONTROL_FLOW_GUARD_XFG)
#define XFG_TRAMPOLINES(ObjectType)\
NDR_SHAREABLE unsigned long ObjectType ## _UserSize_XFG(unsigned long * pFlags, unsigned long Offset, void * pObject)\
{\
return  ObjectType ## _UserSize(pFlags, Offset, (ObjectType *)pObject);\
}\
NDR_SHAREABLE unsigned char * ObjectType ## _UserMarshal_XFG(unsigned long * pFlags, unsigned char * pBuffer, void * pObject)\
{\
return ObjectType ## _UserMarshal(pFlags, pBuffer, (ObjectType *)pObject);\
}\
NDR_SHAREABLE unsigned char * ObjectType ## _UserUnmarshal_XFG(unsigned long * pFlags, unsigned char * pBuffer, void * pObject)\
{\
return ObjectType ## _UserUnmarshal(pFlags, pBuffer, (ObjectType *)pObject);\
}\
NDR_SHAREABLE void ObjectType ## _UserFree_XFG(unsigned long * pFlags, void * pObject)\
{\
ObjectType ## _UserFree(pFlags, (ObjectType *)pObject);\
}
#define XFG_TRAMPOLINES64(ObjectType)\
NDR_SHAREABLE unsigned long ObjectType ## _UserSize64_XFG(unsigned long * pFlags, unsigned long Offset, void * pObject)\
{\
return  ObjectType ## _UserSize64(pFlags, Offset, (ObjectType *)pObject);\
}\
NDR_SHAREABLE unsigned char * ObjectType ## _UserMarshal64_XFG(unsigned long * pFlags, unsigned char * pBuffer, void * pObject)\
{\
return ObjectType ## _UserMarshal64(pFlags, pBuffer, (ObjectType *)pObject);\
}\
NDR_SHAREABLE unsigned char * ObjectType ## _UserUnmarshal64_XFG(unsigned long * pFlags, unsigned char * pBuffer, void * pObject)\
{\
return ObjectType ## _UserUnmarshal64(pFlags, pBuffer, (ObjectType *)pObject);\
}\
NDR_SHAREABLE void ObjectType ## _UserFree64_XFG(unsigned long * pFlags, void * pObject)\
{\
ObjectType ## _UserFree64(pFlags, (ObjectType *)pObject);\
}
#define XFG_BIND_TRAMPOLINES(HandleType, ObjectType)\
static void* ObjectType ## _bind_XFG(HandleType pObject)\
{\
return ObjectType ## _bind((ObjectType) pObject);\
}\
static void ObjectType ## _unbind_XFG(HandleType pObject, handle_t ServerHandle)\
{\
ObjectType ## _unbind((ObjectType) pObject, ServerHandle);\
}
#define XFG_TRAMPOLINE_FPTR(Function) Function ## _XFG
#define XFG_TRAMPOLINE_FPTR_DEPENDENT_SYMBOL(Symbol) Symbol ## _XFG
#else
#define XFG_TRAMPOLINES(ObjectType)
#define XFG_TRAMPOLINES64(ObjectType)
#define XFG_BIND_TRAMPOLINES(HandleType, ObjectType)
#define XFG_TRAMPOLINE_FPTR(Function) Function
#define XFG_TRAMPOLINE_FPTR_DEPENDENT_SYMBOL(Symbol) Symbol
#endif



extern const IConsoleHandoff_MIDL_TYPE_FORMAT_STRING IConsoleHandoff__MIDL_TypeFormatString;
extern const IConsoleHandoff_MIDL_PROC_FORMAT_STRING IConsoleHandoff__MIDL_ProcFormatString;
extern const IConsoleHandoff_MIDL_EXPR_FORMAT_STRING IConsoleHandoff__MIDL_ExprFormatString;

#ifdef __cplusplus
namespace {
#endif

extern const MIDL_STUB_DESC Object_StubDesc;
#ifdef __cplusplus
}
#endif


extern const MIDL_SERVER_INFO IConsoleHandoff_ServerInfo;
extern const MIDL_STUBLESS_PROXY_INFO IConsoleHandoff_ProxyInfo;

#ifdef __cplusplus
namespace {
#endif

extern const MIDL_STUB_DESC Object_StubDesc;
#ifdef __cplusplus
}
#endif


extern const MIDL_SERVER_INFO IDefaultTerminalMarker_ServerInfo;
extern const MIDL_STUBLESS_PROXY_INFO IDefaultTerminalMarker_ProxyInfo;



#if !defined(__RPC_ARM64__)
#error  Invalid build platform for this stub.
#endif

static const IConsoleHandoff_MIDL_PROC_FORMAT_STRING IConsoleHandoff__MIDL_ProcFormatString =
    {
        0,
        {

	/* Procedure EstablishHandoff */

			0x33,		/* FC_AUTO_HANDLE */
			0x6c,		/* Old Flags:  object, Oi2 */
/*  2 */	NdrFcLong( 0x0 ),	/* 0 */
/*  6 */	NdrFcShort( 0x3 ),	/* 3 */
/*  8 */	NdrFcShort( 0x40 ),	/* ARM64 Stack size/offset = 64 */
/* 10 */	NdrFcShort( 0x0 ),	/* 0 */
/* 12 */	NdrFcShort( 0x8 ),	/* 8 */
/* 14 */	0x47,		/* Oi2 Flags:  srv must size, clt must size, has return, has ext, */
			0x7,		/* 7 */
/* 16 */	0x12,		/* 18 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 18 */	NdrFcShort( 0x0 ),	/* 0 */
/* 20 */	NdrFcShort( 0x0 ),	/* 0 */
/* 22 */	NdrFcShort( 0x0 ),	/* 0 */
/* 24 */	NdrFcShort( 0x7 ),	/* 7 */
/* 26 */	0x7,		/* 7 */
			0x80,		/* 128 */
/* 28 */	0x81,		/* 129 */
			0x82,		/* 130 */
/* 30 */	0x83,		/* 131 */
			0x84,		/* 132 */
/* 32 */	0x85,		/* 133 */
			0x86,		/* 134 */

	/* Parameter server */

/* 34 */	NdrFcShort( 0x8b ),	/* Flags:  must size, must free, in, by val, */
/* 36 */	NdrFcShort( 0x8 ),	/* ARM64 Stack size/offset = 8 */
/* 38 */	NdrFcShort( 0x2 ),	/* Type Offset=2 */

	/* Parameter inputEvent */

/* 40 */	NdrFcShort( 0x8b ),	/* Flags:  must size, must free, in, by val, */
/* 42 */	NdrFcShort( 0x10 ),	/* ARM64 Stack size/offset = 16 */
/* 44 */	NdrFcShort( 0x8 ),	/* Type Offset=8 */

	/* Parameter msg */

/* 46 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 48 */	NdrFcShort( 0x18 ),	/* ARM64 Stack size/offset = 24 */
/* 50 */	NdrFcShort( 0x12 ),	/* Type Offset=18 */

	/* Parameter signalPipe */

/* 52 */	NdrFcShort( 0x8b ),	/* Flags:  must size, must free, in, by val, */
/* 54 */	NdrFcShort( 0x20 ),	/* ARM64 Stack size/offset = 32 */
/* 56 */	NdrFcShort( 0x24 ),	/* Type Offset=36 */

	/* Parameter inboxProcess */

/* 58 */	NdrFcShort( 0x8b ),	/* Flags:  must size, must free, in, by val, */
/* 60 */	NdrFcShort( 0x28 ),	/* ARM64 Stack size/offset = 40 */
/* 62 */	NdrFcShort( 0x2a ),	/* Type Offset=42 */

	/* Parameter process */

/* 64 */	NdrFcShort( 0x2113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=8 */
/* 66 */	NdrFcShort( 0x30 ),	/* ARM64 Stack size/offset = 48 */
/* 68 */	NdrFcShort( 0x2a ),	/* Type Offset=42 */

	/* Return value */

/* 70 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 72 */	NdrFcShort( 0x38 ),	/* ARM64 Stack size/offset = 56 */
/* 74 */	0x8,		/* FC_LONG */
			0x0,		/* 0 */

			0x0
        }
    };

static const IConsoleHandoff_MIDL_TYPE_FORMAT_STRING IConsoleHandoff__MIDL_TypeFormatString =
    {
        0,
        {
			NdrFcShort( 0x0 ),	/* 0 */
/*  2 */	0x3c,		/* FC_SYSTEM_HANDLE */
			0x0,		/* 0 */
/*  4 */	NdrFcLong( 0x0 ),	/* 0 */
/*  8 */	0x3c,		/* FC_SYSTEM_HANDLE */
			0x2,		/* 2 */
/* 10 */	NdrFcLong( 0x0 ),	/* 0 */
/* 14 */	
			0x11, 0x0,	/* FC_RP */
/* 16 */	NdrFcShort( 0x2 ),	/* Offset= 2 (18) */
/* 18 */	
			0x1a,		/* FC_BOGUS_STRUCT */
			0x7,		/* 7 */
/* 20 */	NdrFcShort( 0x28 ),	/* 40 */
/* 22 */	NdrFcShort( 0x0 ),	/* 0 */
/* 24 */	NdrFcShort( 0x0 ),	/* Offset= 0 (24) */
/* 26 */	0x8,		/* FC_LONG */
			0x8,		/* FC_LONG */
/* 28 */	0xb,		/* FC_HYPER */
			0xb,		/* FC_HYPER */
/* 30 */	0x8,		/* FC_LONG */
			0x8,		/* FC_LONG */
/* 32 */	0x8,		/* FC_LONG */
			0x40,		/* FC_STRUCTPAD4 */
/* 34 */	0x5c,		/* FC_PAD */
			0x5b,		/* FC_END */
/* 36 */	0x3c,		/* FC_SYSTEM_HANDLE */
			0xc,		/* 12 */
/* 38 */	NdrFcLong( 0x0 ),	/* 0 */
/* 42 */	0x3c,		/* FC_SYSTEM_HANDLE */
			0x4,		/* 4 */
/* 44 */	NdrFcLong( 0x0 ),	/* 0 */
/* 48 */	
			0x11, 0x4,	/* FC_RP [alloced_on_stack] */
/* 50 */	NdrFcShort( 0xfff8 ),	/* Offset= -8 (42) */

			0x0
        }
    };


/* Standard interface: __MIDL_itf_IConsoleHandoff_0000_0000, ver. 0.0,
   GUID={0x00000000,0x0000,0x0000,{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}} */


/* Object interface: IUnknown, ver. 0.0,
   GUID={0x00000000,0x0000,0x0000,{0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}} */


/* Object interface: IConsoleHandoff, ver. 0.0,
   GUID={0xE686C757,0x9A35,0x4A1C,{0xB3,0xCE,0x0B,0xCC,0x8B,0x5C,0x69,0xF4}} */

#pragma code_seg(".orpc")
static const unsigned short IConsoleHandoff_FormatStringOffsetTable[] =
    {
    0
    };



/* Object interface: IDefaultTerminalMarker, ver. 0.0,
   GUID={0x746E6BC0,0xAB05,0x4E38,{0xAB,0x14,0x71,0xE8,0x67,0x63,0x14,0x1F}} */

#pragma code_seg(".orpc")
static const unsigned short IDefaultTerminalMarker_FormatStringOffsetTable[] =
    {
    0
    };



#endif /* defined(_M_ARM64) */



/* this ALWAYS GENERATED file contains the proxy stub code */


 /* File created by MIDL compiler version 8.01.0628 */
/* at Tue Jan 19 11:14:07 2038
 */
/* Compiler settings for IConsoleHandoff.idl:
    Oicf, W1, Zp8, env=Win64 (32b run), target_arch=ARM64 8.01.0628 
    protocol : all , ms_ext, c_ext, robust
    error checks: allocation ref bounds_check enum stub_data 
    VC __declspec() decoration level: 
         __declspec(uuid()), __declspec(selectany), __declspec(novtable)
         DECLSPEC_UUID(), MIDL_INTERFACE()
*/
/* @@MIDL_FILE_HEADING(  ) */

#if defined(_M_ARM64)




#if !defined(__RPC_ARM64__)
#error  Invalid build platform for this stub.
#endif


#include "ndr64types.h"
#include "pshpack8.h"
#ifdef __cplusplus
namespace {
#endif


typedef 
NDR64_FORMAT_CHAR
__midl_frag11_t;
extern const __midl_frag11_t __midl_frag11;

typedef 
struct _NDR64_SYSTEM_HANDLE_FORMAT
__midl_frag10_t;
extern const __midl_frag10_t __midl_frag10;

typedef 
struct _NDR64_POINTER_FORMAT
__midl_frag9_t;
extern const __midl_frag9_t __midl_frag9;

typedef 
struct _NDR64_SYSTEM_HANDLE_FORMAT
__midl_frag7_t;
extern const __midl_frag7_t __midl_frag7;

typedef 
struct 
{
    struct _NDR64_STRUCTURE_HEADER_FORMAT frag1;
}
__midl_frag6_t;
extern const __midl_frag6_t __midl_frag6;

typedef 
struct _NDR64_POINTER_FORMAT
__midl_frag5_t;
extern const __midl_frag5_t __midl_frag5;

typedef 
struct _NDR64_SYSTEM_HANDLE_FORMAT
__midl_frag4_t;
extern const __midl_frag4_t __midl_frag4;

typedef 
struct _NDR64_SYSTEM_HANDLE_FORMAT
__midl_frag3_t;
extern const __midl_frag3_t __midl_frag3;

typedef 
struct 
{
    struct _NDR64_PROC_FORMAT frag1;
    struct _NDR64_PARAM_FORMAT frag2;
    struct _NDR64_PARAM_FORMAT frag3;
    struct _NDR64_PARAM_FORMAT frag4;
    struct _NDR64_PARAM_FORMAT frag5;
    struct _NDR64_PARAM_FORMAT frag6;
    struct _NDR64_PARAM_FORMAT frag7;
    struct _NDR64_PARAM_FORMAT frag8;
    struct /* ARM Parameter Layout */
    {
        NDR64_UINT16 frag1;
        NDR64_UINT8 frag2;
        NDR64_UINT8 frag3[ 7 ];
    } frag9;
}
__midl_frag2_t;
extern const __midl_frag2_t __midl_frag2;

typedef 
NDR64_FORMAT_UINT32
__midl_frag1_t;
extern const __midl_frag1_t __midl_frag1;

static const __midl_frag11_t __midl_frag11 =
0x5    /* FC64_INT32 */;

static const __midl_frag10_t __midl_frag10 =
{ 
/* HANDLE */
    0x3c,    /* FC64_SYSTEM_HANDLE */
    (NDR64_UINT8) 4 /* 0x4 */,
    (NDR64_UINT32) 0 /* 0x0 */,
};

static const __midl_frag9_t __midl_frag9 =
{ 
/* *HANDLE */
    0x20,    /* FC64_RP */
    (NDR64_UINT8) 4 /* 0x4 */,
    (NDR64_UINT16) 0 /* 0x0 */,
    &__midl_frag10
};

static const __midl_frag7_t __midl_frag7 =
{ 
/* HANDLE */
    0x3c,    /* FC64_SYSTEM_HANDLE */
    (NDR64_UINT8) 12 /* 0xc */,
    (NDR64_UINT32) 0 /* 0x0 */,
};

static const __midl_frag6_t __midl_frag6 =
{ 
/* CONSOLE_PORTABLE_ATTACH_MSG */
    { 
    /* CONSOLE_PORTABLE_ATTACH_MSG */
        0x30,    /* FC64_STRUCT */
        (NDR64_UINT8) 7 /* 0x7 */,
        { 
        /* CONSOLE_PORTABLE_ATTACH_MSG */
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0
        },
        (NDR64_UINT8) 0 /* 0x0 */,
        (NDR64_UINT32) 40 /* 0x28 */
    }
};

static const __midl_frag5_t __midl_frag5 =
{ 
/* *CONSOLE_PORTABLE_ATTACH_MSG */
    0x20,    /* FC64_RP */
    (NDR64_UINT8) 0 /* 0x0 */,
    (NDR64_UINT16) 0 /* 0x0 */,
    &__midl_frag6
};

static const __midl_frag4_t __midl_frag4 =
{ 
/* HANDLE */
    0x3c,    /* FC64_SYSTEM_HANDLE */
    (NDR64_UINT8) 2 /* 0x2 */,
    (NDR64_UINT32) 0 /* 0x0 */,
};

static const __midl_frag3_t __midl_frag3 =
{ 
/* HANDLE */
    0x3c,    /* FC64_SYSTEM_HANDLE */
    (NDR64_UINT8) 0 /* 0x0 */,
    (NDR64_UINT32) 0 /* 0x0 */,
};

static const __midl_frag2_t __midl_frag2 =
{ 
/* EstablishHandoff */
    { 
    /* EstablishHandoff */      /* procedure EstablishHandoff */
        (NDR64_UINT32) 68026691 /* 0x40e0143 */,    /* auto handle */ /* IsIntrepreted, [object], ServerMustSize, ClientMustSize, HasReturn, HasArmParamLayout */
        (NDR64_UINT32) 64 /* 0x40 */ ,  /* Stack size */
        (NDR64_UINT32) 80 /* 0x50 */,
        (NDR64_UINT32) 8 /* 0x8 */,
        (NDR64_UINT16) 0 /* 0x0 */,
        (NDR64_UINT16) 0 /* 0x0 */,
        (NDR64_UINT16) 7 /* 0x7 */,
        (NDR64_UINT16) 0 /* 0x0 */
    },
    { 
    /* server */      /* parameter server */
        &__midl_frag3,
        { 
        /* server */
            1,
            1,
            0,
            1,
            0,
            0,
            0,
            1,
            0,
            0,
            0,
            0,
            0,
            (NDR64_UINT16) 0 /* 0x0 */,
            0
        },    /* MustSize, MustFree, [in], ByValue */
        (NDR64_UINT16) 0 /* 0x0 */,
        8 /* 0x8 */,   /* Stack offset */
    },
    { 
    /* inputEvent */      /* parameter inputEvent */
        &__midl_frag4,
        { 
        /* inputEvent */
            1,
            1,
            0,
            1,
            0,
            0,
            0,
            1,
            0,
            0,
            0,
            0,
            0,
            (NDR64_UINT16) 0 /* 0x0 */,
            0
        },    /* MustSize, MustFree, [in], ByValue */
        (NDR64_UINT16) 0 /* 0x0 */,
        16 /* 0x10 */,   /* Stack offset */
    },
    { 
    /* msg */      /* parameter msg */
        &__midl_frag6,
        { 
        /* msg */
            0,
            1,
            0,
            1,
            0,
            0,
            0,
            0,
            1,
            0,
            0,
            0,
            0,
            (NDR64_UINT16) 0 /* 0x0 */,
            0
        },    /* MustFree, [in], SimpleRef */
        (NDR64_UINT16) 0 /* 0x0 */,
        24 /* 0x18 */,   /* Stack offset */
    },
    { 
    /* signalPipe */      /* parameter signalPipe */
        &__midl_frag7,
        { 
        /* signalPipe */
            1,
            1,
            0,
            1,
            0,
            0,
            0,
            1,
            0,
            0,
            0,
            0,
            0,
            (NDR64_UINT16) 0 /* 0x0 */,
            0
        },    /* MustSize, MustFree, [in], ByValue */
        (NDR64_UINT16) 0 /* 0x0 */,
        32 /* 0x20 */,   /* Stack offset */
    },
    { 
    /* inboxProcess */      /* parameter inboxProcess */
        &__midl_frag10,
        { 
        /* inboxProcess */
            1,
            1,
            0,
            1,
            0,
            0,
            0,
            1,
            0,
            0,
            0,
            0,
            0,
            (NDR64_UINT16) 0 /* 0x0 */,
            0
        },    /* MustSize, MustFree, [in], ByValue */
        (NDR64_UINT16) 0 /* 0x0 */,
        40 /* 0x28 */,   /* Stack offset */
    },
    { 
    /* process */      /* parameter process */
        &__midl_frag10,
        { 
        /* process */
            1,
            1,
            0,
            0,
            1,
            0,
            0,
            0,
            1,
            0,
            0,
            0,
            0,
            (NDR64_UINT16) 0 /* 0x0 */,
            1
        },    /* MustSize, MustFree, [out], SimpleRef, UseCache */
        (NDR64_UINT16) 0 /* 0x0 */,
        48 /* 0x30 */,   /* Stack offset */
    },
    { 
    /* HRESULT */      /* parameter HRESULT */
        &__midl_frag11,
        { 
        /* HRESULT */
            0,
            0,
            0,
            0,
            1,
            1,
            1,
            1,
            0,
            0,
            0,
            0,
            0,
            (NDR64_UINT16) 0 /* 0x0 */,
            0
        },    /* [out], IsReturn, Basetype, ByValue */
        (NDR64_UINT16) 0 /* 0x0 */,
        56 /* 0x38 */,   /* Stack offset */
    },
    { 
    /* EstablishHandoff */      /* ARM register placement data */
        (NDR64_UINT16) 7 /* 0x7 */ ,  /* Number of Entries */
        (NDR64_UINT8) 7 /* 0x7 */ ,  /* Slots Used */
        { 
        /* EstablishHandoff */      /* Placement data octets */
            (NDR64_UINT8) 0x80, 
            (NDR64_UINT8) 0x81, 
            (NDR64_UINT8) 0x82, 
            (NDR64_UINT8) 0x83, 
            (NDR64_UINT8) 0x84, 
            (NDR64_UINT8) 0x85, 
            (NDR64_UINT8) 0x86
        }
    }
};

static const __midl_frag1_t __midl_frag1 =
(NDR64_UINT32) 0 /* 0x0 */;
#ifdef __cplusplus
}
#endif


#include "poppack.h"



/* Standard interface: __MIDL_itf_IConsoleHandoff_0000_0000, ver. 0.0,
   GUID={0x00000000,0x0000,0x0000,{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}} */


/* Object interface: IUnknown, ver. 0.0,
   GUID={0x00000000,0x0000,0x0000,{0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}} */


/* Object interface: IConsoleHandoff, ver. 0.0,
   GUID={0xE686C757,0x9A35,0x4A1C,{0xB3,0xCE,0x0B,0xCC,0x8B,0x5C,0x69,0xF4}} */

#pragma code_seg(".orpc")
static const FormatInfoRef IConsoleHandoff_Ndr64ProcTable[] =
    {
    &__midl_frag2
    };


static const MIDL_SYNTAX_INFO IConsoleHandoff_SyntaxInfo [  2 ] = 
    {
    {
    {{0x8A885D04,0x1CEB,0x11C9,{0x9F,0xE8,0x08,0x00,0x2B,0x10,0x48,0x60}},{2,0}},
    0,
    IConsoleHandoff__MIDL_ProcFormatString.Format,
    &IConsoleHandoff_FormatStringOffsetTable[-3],
    IConsoleHandoff__MIDL_TypeFormatString.Format,
    0,
    0,
    0
    }
    ,{
    {{0x71710533,0xbeba,0x4937,{0x83,0x19,0xb5,0xdb,0xef,0x9c,0xcc,0x36}},{1,0}},
    0,
    0 ,
    (unsigned short *) &IConsoleHandoff_Ndr64ProcTable[-3],
    0,
    0,
    0,
    0
    }
    };

static const MIDL_STUBLESS_PROXY_INFO IConsoleHandoff_ProxyInfo =
    {
    &Object_StubDesc,
    IConsoleHandoff__MIDL_ProcFormatString.Format,
    &IConsoleHandoff_FormatStringOffsetTable[-3],
    (RPC_SYNTAX_IDENTIFIER*)&_RpcTransferSyntax_2_0,
    2,
    (MIDL_SYNTAX_INFO*)IConsoleHandoff_SyntaxInfo
    
    };


static const MIDL_SERVER_INFO IConsoleHandoff_ServerInfo = 
    {
    &Object_StubDesc,
    0,
    IConsoleHandoff__MIDL_ProcFormatString.Format,
    (unsigned short *) &IConsoleHandoff_FormatStringOffsetTable[-3],
    0,
    (RPC_SYNTAX_IDENTIFIER*)&_NDR64_RpcTransferSyntax_1_0,
    2,
    (MIDL_SYNTAX_INFO*)IConsoleHandoff_SyntaxInfo
    };
const CINTERFACE_PROXY_VTABLE(4) _IConsoleHandoffProxyVtbl = 
{
    &IConsoleHandoff_ProxyInfo,
    &IID_IConsoleHandoff,
    IUnknown_QueryInterface_Proxy,
    IUnknown_AddRef_Proxy,
    IUnknown_Release_Proxy ,
    ObjectStublessClient3 /* IConsoleHandoff::EstablishHandoff */
};

const CInterfaceStubVtbl _IConsoleHandoffStubVtbl =
{
    &IID_IConsoleHandoff,
    &IConsoleHandoff_ServerInfo,
    4,
    0, /* pure interpreted */
    CStdStubBuffer_METHODS_OPT
};


/* Object interface: IDefaultTerminalMarker, ver. 0.0,
   GUID={0x746E6BC0,0xAB05,0x4E38,{0xAB,0x14,0x71,0xE8,0x67,0x63,0x14,0x1F}} */

#pragma code_seg(".orpc")
static const FormatInfoRef IDefaultTerminalMarker_Ndr64ProcTable[] =
    {
    0
    };


static const MIDL_SYNTAX_INFO IDefaultTerminalMarker_SyntaxInfo [  2 ] = 
    {
    {
    {{0x8A885D04,0x1CEB,0x11C9,{0x9F,0xE8,0x08,0x00,0x2B,0x10,0x48,0x60}},{2,0}},
    0,
    IConsoleHandoff__MIDL_ProcFormatString.Format,
    &IDefaultTerminalMarker_FormatStringOffsetTable[-3],
    IConsoleHandoff__MIDL_TypeFormatString.Format,
    0,
    0,
    0
    }
    ,{
    {{0x71710533,0xbeba,0x4937,{0x83,0x19,0xb5,0xdb,0xef,0x9c,0xcc,0x36}},{1,0}},
    0,
    0 ,
    (unsigned short *) &IDefaultTerminalMarker_Ndr64ProcTable[-3],
    0,
    0,
    0,
    0
    }
    };

static const MIDL_STUBLESS_PROXY_INFO IDefaultTerminalMarker_ProxyInfo =
    {
    &Object_StubDesc,
    IConsoleHandoff__MIDL_ProcFormatString.Format,
    &IDefaultTerminalMarker_FormatStringOffsetTable[-3],
    (RPC_SYNTAX_IDENTIFIER*)&_RpcTransferSyntax_2_0,
    2,
    (MIDL_SYNTAX_INFO*)IDefaultTerminalMarker_SyntaxInfo
    
    };


static const MIDL_SERVER_INFO IDefaultTerminalMarker_ServerInfo = 
    {
    &Object_StubDesc,
    0,
    IConsoleHandoff__MIDL_ProcFormatString.Format,
    (unsigned short *) &IDefaultTerminalMarker_FormatStringOffsetTable[-3],
    0,
    (RPC_SYNTAX_IDENTIFIER*)&_NDR64_RpcTransferSyntax_1_0,
    2,
    (MIDL_SYNTAX_INFO*)IDefaultTerminalMarker_SyntaxInfo
    };
const CINTERFACE_PROXY_VTABLE(3) _IDefaultTerminalMarkerProxyVtbl = 
{
    0,
    &IID_IDefaultTerminalMarker,
    IUnknown_QueryInterface_Proxy,
    IUnknown_AddRef_Proxy,
    IUnknown_Release_Proxy
};

const CInterfaceStubVtbl _IDefaultTerminalMarkerStubVtbl =
{
    &IID_IDefaultTerminalMarker,
    &IDefaultTerminalMarker_ServerInfo,
    3,
    0, /* pure interpreted */
    CStdStubBuffer_METHODS_OPT
};

#ifdef __cplusplus
namespace {
#endif
static const MIDL_STUB_DESC Object_StubDesc = 
    {
    0,
    NdrOleAllocate,
    NdrOleFree,
    0,
    0,
    0,
    0,
    0,
    IConsoleHandoff__MIDL_TypeFormatString.Format,
    1, /* -error bounds_check flag */
    0xa0000, /* Ndr library version */
    0,
    0x8010274, /* MIDL Version 8.1.628 */
    0,
    0,
    0,  /* notify & notify_flag routine table */
    0x2000001, /* MIDL flag */
    0, /* cs routines */
    0,   /* proxy/server info */
    0
    };
#ifdef __cplusplus
}
#endif

const CInterfaceProxyVtbl * const _IConsoleHandoff_ProxyVtblList[] = 
{
    ( CInterfaceProxyVtbl *) &_IConsoleHandoffProxyVtbl,
    ( CInterfaceProxyVtbl *) &_IDefaultTerminalMarkerProxyVtbl,
    0
};

const CInterfaceStubVtbl * const _IConsoleHandoff_StubVtblList[] = 
{
    ( CInterfaceStubVtbl *) &_IConsoleHandoffStubVtbl,
    ( CInterfaceStubVtbl *) &_IDefaultTerminalMarkerStubVtbl,
    0
};

PCInterfaceName const _IConsoleHandoff_InterfaceNamesList[] = 
{
    "IConsoleHandoff",
    "IDefaultTerminalMarker",
    0
};


#define _IConsoleHandoff_CHECK_IID(n)	IID_GENERIC_CHECK_IID( _IConsoleHandoff, pIID, n)

int __stdcall _IConsoleHandoff_IID_Lookup( const IID * pIID, int * pIndex )
{
    IID_BS_LOOKUP_SETUP

    IID_BS_LOOKUP_INITIAL_TEST( _IConsoleHandoff, 2, 1 )
    IID_BS_LOOKUP_RETURN_RESULT( _IConsoleHandoff, 2, *pIndex )
    
}

EXTERN_C const ExtendedProxyFileInfo IConsoleHandoff_ProxyFileInfo = 
{
    (PCInterfaceProxyVtblList *) & _IConsoleHandoff_ProxyVtblList,
    (PCInterfaceStubVtblList *) & _IConsoleHandoff_StubVtblList,
    (const PCInterfaceName * ) & _IConsoleHandoff_InterfaceNamesList,
    0, /* no delegation */
    & _IConsoleHandoff_IID_Lookup, 
    2,
    2,
    0, /* table of [async_uuid] interfaces */
    0, /* Filler1 */
    0, /* Filler2 */
    0  /* Filler3 */
};
#if _MSC_VER >= 1200
#pragma warning(pop)
#endif


#endif /* defined(_M_ARM64) */

