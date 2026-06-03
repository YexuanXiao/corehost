

/* this ALWAYS GENERATED file contains the definitions for the interfaces */


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



/* verify that the <rpcndr.h> version is high enough to compile this file*/
#ifndef __REQUIRED_RPCNDR_H_VERSION__
#define __REQUIRED_RPCNDR_H_VERSION__ 500
#endif

#include "rpc.h"
#include "rpcndr.h"

#ifndef __RPCNDR_H_VERSION__
#error this stub requires an updated version of <rpcndr.h>
#endif /* __RPCNDR_H_VERSION__ */

#ifndef COM_NO_WINDOWS_H
#include "windows.h"
#include "ole2.h"
#endif /*COM_NO_WINDOWS_H*/

#ifndef __IConsoleHandoff_h__
#define __IConsoleHandoff_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif

#ifndef DECLSPEC_XFGVIRT
#if defined(_CONTROL_FLOW_GUARD_XFG)
#define DECLSPEC_XFGVIRT(base, func) __declspec(xfg_virtual(base, func))
#else
#define DECLSPEC_XFGVIRT(base, func)
#endif
#endif

/* Forward Declarations */ 

#ifndef __IConsoleHandoff_FWD_DEFINED__
#define __IConsoleHandoff_FWD_DEFINED__
typedef interface IConsoleHandoff IConsoleHandoff;

#endif 	/* __IConsoleHandoff_FWD_DEFINED__ */


#ifndef __IDefaultTerminalMarker_FWD_DEFINED__
#define __IDefaultTerminalMarker_FWD_DEFINED__
typedef interface IDefaultTerminalMarker IDefaultTerminalMarker;

#endif 	/* __IDefaultTerminalMarker_FWD_DEFINED__ */


/* header files for imported files */
#include "unknwn.h"

#ifdef __cplusplus
extern "C"{
#endif 


/* interface __MIDL_itf_IConsoleHandoff_0000_0000 */
/* [local] */ 

typedef struct _CONSOLE_PORTABLE_ATTACH_MSG
    {
    DWORD IdLowPart;
    LONG IdHighPart;
    ULONG64 Process;
    ULONG64 Object;
    ULONG Function;
    ULONG InputSize;
    ULONG OutputSize;
    } 	CONSOLE_PORTABLE_ATTACH_MSG;

typedef CONSOLE_PORTABLE_ATTACH_MSG *PCONSOLE_PORTABLE_ATTACH_MSG;

typedef const CONSOLE_PORTABLE_ATTACH_MSG *PCCONSOLE_PORTABLE_ATTACH_MSG;



extern RPC_IF_HANDLE __MIDL_itf_IConsoleHandoff_0000_0000_v0_0_c_ifspec;
extern RPC_IF_HANDLE __MIDL_itf_IConsoleHandoff_0000_0000_v0_0_s_ifspec;

#ifndef __IConsoleHandoff_INTERFACE_DEFINED__
#define __IConsoleHandoff_INTERFACE_DEFINED__

/* interface IConsoleHandoff */
/* [uuid][object] */ 


EXTERN_C const IID IID_IConsoleHandoff;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("E686C757-9A35-4A1C-B3CE-0BCC8B5C69F4")
    IConsoleHandoff : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE EstablishHandoff( 
            /* [system_handle][in] */ HANDLE server,
            /* [system_handle][in] */ HANDLE inputEvent,
            /* [ref][in] */ PCCONSOLE_PORTABLE_ATTACH_MSG msg,
            /* [system_handle][in] */ HANDLE signalPipe,
            /* [system_handle][in] */ HANDLE inboxProcess,
            /* [system_handle][out] */ HANDLE *process) = 0;
        
    };
    
    
#else 	/* C style interface */

    typedef struct IConsoleHandoffVtbl
    {
        BEGIN_INTERFACE
        
        DECLSPEC_XFGVIRT(IUnknown, QueryInterface)
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IConsoleHandoff * This,
            /* [in] */ REFIID riid,
            /* [annotation][iid_is][out] */ 
            _COM_Outptr_  void **ppvObject);
        
        DECLSPEC_XFGVIRT(IUnknown, AddRef)
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IConsoleHandoff * This);
        
        DECLSPEC_XFGVIRT(IUnknown, Release)
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IConsoleHandoff * This);
        
        DECLSPEC_XFGVIRT(IConsoleHandoff, EstablishHandoff)
        HRESULT ( STDMETHODCALLTYPE *EstablishHandoff )( 
            IConsoleHandoff * This,
            /* [system_handle][in] */ HANDLE server,
            /* [system_handle][in] */ HANDLE inputEvent,
            /* [ref][in] */ PCCONSOLE_PORTABLE_ATTACH_MSG msg,
            /* [system_handle][in] */ HANDLE signalPipe,
            /* [system_handle][in] */ HANDLE inboxProcess,
            /* [system_handle][out] */ HANDLE *process);
        
        END_INTERFACE
    } IConsoleHandoffVtbl;

    interface IConsoleHandoff
    {
        CONST_VTBL struct IConsoleHandoffVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IConsoleHandoff_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IConsoleHandoff_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IConsoleHandoff_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IConsoleHandoff_EstablishHandoff(This,server,inputEvent,msg,signalPipe,inboxProcess,process)	\
    ( (This)->lpVtbl -> EstablishHandoff(This,server,inputEvent,msg,signalPipe,inboxProcess,process) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IConsoleHandoff_INTERFACE_DEFINED__ */


#ifndef __IDefaultTerminalMarker_INTERFACE_DEFINED__
#define __IDefaultTerminalMarker_INTERFACE_DEFINED__

/* interface IDefaultTerminalMarker */
/* [uuid][object] */ 


EXTERN_C const IID IID_IDefaultTerminalMarker;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("746E6BC0-AB05-4E38-AB14-71E86763141F")
    IDefaultTerminalMarker : public IUnknown
    {
    public:
    };
    
    
#else 	/* C style interface */

    typedef struct IDefaultTerminalMarkerVtbl
    {
        BEGIN_INTERFACE
        
        DECLSPEC_XFGVIRT(IUnknown, QueryInterface)
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDefaultTerminalMarker * This,
            /* [in] */ REFIID riid,
            /* [annotation][iid_is][out] */ 
            _COM_Outptr_  void **ppvObject);
        
        DECLSPEC_XFGVIRT(IUnknown, AddRef)
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDefaultTerminalMarker * This);
        
        DECLSPEC_XFGVIRT(IUnknown, Release)
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDefaultTerminalMarker * This);
        
        END_INTERFACE
    } IDefaultTerminalMarkerVtbl;

    interface IDefaultTerminalMarker
    {
        CONST_VTBL struct IDefaultTerminalMarkerVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDefaultTerminalMarker_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDefaultTerminalMarker_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDefaultTerminalMarker_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDefaultTerminalMarker_INTERFACE_DEFINED__ */


/* Additional Prototypes for ALL interfaces */

/* end of Additional Prototypes */

#ifdef __cplusplus
}
#endif

#endif


