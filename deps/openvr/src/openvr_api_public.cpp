//========= Copyright Valve Corporation ============//
#define VR_API_EXPORT 1
#include "openvr.h"
#include "ivrclientcore.h"
#include <vrcore/pathtools_public.h>
#include <vrcore/sharedlibtools_public.h>
#include <vrcore/envvartools_public.h>
#include "hmderrors_public.h"
#include <vrcore/strtools_public.h>
#include <vrcore/vrpathregistry_public.h>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#if defined( _WIN32 )
#include <windows.h>
#endif

using vr::EVRInitError;
using vr::IVRSystem;
using vr::IVRClientCore;
using vr::VRInitError_None;

// figure out how to import from the VR API dll
#if defined(_WIN32)

#if !defined(OPENVR_BUILD_STATIC)
#define VR_EXPORT_INTERFACE extern "C" __declspec( dllexport )
#else
#define VR_EXPORT_INTERFACE extern "C"
#endif

#elif defined(__GNUC__) || defined(COMPILER_GCC) || defined(__APPLE__)

#define VR_EXPORT_INTERFACE extern "C" __attribute__((visibility("default")))

#else
#error "Unsupported Platform."
#endif

namespace vr
{

static void *g_pVRModule = NULL;
static IVRClientCore *g_pHmdSystem = NULL;
static std::recursive_mutex g_mutexSystem;


typedef void* (*VRClientCoreFactoryFn)(const char *pInterfaceName, int *pReturnCode);

static uint32_t g_nVRToken = 0;

static void H2V_OpenVR_Trace( const char *pchPhase )
{
	const char *pchEnabled = std::getenv( "H2V_OPENVR_TRACE" );
	if ( pchEnabled == nullptr || pchEnabled[0] == '\0' || pchEnabled[0] == '0' )
		return;
	std::fprintf( stderr, "openvr_phase=%s\n", pchPhase );
	std::fflush( stderr );
}

static void H2V_OpenVR_TraceText( const char *pchKey, const std::string& value )
{
	const char *pchEnabled = std::getenv( "H2V_OPENVR_TRACE" );
	if ( pchEnabled == nullptr || pchEnabled[0] == '\0' || pchEnabled[0] == '0' )
		return;
	std::fprintf( stderr, "openvr_%s=%s\n", pchKey, value.c_str() );
	std::fflush( stderr );
}

#if defined( _WIN32 )
class H2V_OpenVR_DependencyDirectory final
{
public:
	explicit H2V_OpenVR_DependencyDirectory( const std::string& directory )
	{
		const auto required = GetDllDirectoryW( 0, nullptr );
		if ( required != 0 )
		{
			previous_.resize( required );
			if ( GetDllDirectoryW( required, previous_.data() ) == 0 ) previous_.clear();
		}
		const auto path = UTF8to16( directory );
		active_ = SetDllDirectoryW( path.c_str() ) != FALSE;
	}

	~H2V_OpenVR_DependencyDirectory()
	{
		if ( active_ ) SetDllDirectoryW( previous_.empty() ? nullptr : previous_.c_str() );
	}

	H2V_OpenVR_DependencyDirectory( const H2V_OpenVR_DependencyDirectory& ) = delete;
	H2V_OpenVR_DependencyDirectory& operator=( const H2V_OpenVR_DependencyDirectory& ) = delete;

private:
	std::wstring previous_;
	bool active_{};
};
#endif

uint32_t VR_GetInitToken()
{
	return g_nVRToken;
}

EVRInitError VR_LoadHmdSystemInternal();
void CleanupInternalInterfaces();

uint32_t VR_InitInternal2( EVRInitError *peError, vr::EVRApplicationType eApplicationType, const char *pStartupInfo )
{
	std::lock_guard<std::recursive_mutex> lock( g_mutexSystem );

	H2V_OpenVR_Trace( "load-client-enter" );
	EVRInitError err = VR_LoadHmdSystemInternal();
	H2V_OpenVR_Trace( "load-client-returned" );
	if ( err == vr::VRInitError_None )
	{
		H2V_OpenVR_Trace( "client-core-init-enter" );
		err = g_pHmdSystem->Init( eApplicationType, pStartupInfo );
		H2V_OpenVR_Trace( "client-core-init-returned" );
	}

	if ( peError )
		*peError = err;

	if ( err != VRInitError_None )
	{
		SharedLib_Unload( g_pVRModule );
		g_pHmdSystem = NULL;
		g_pVRModule = NULL;

		return 0;
	}

	return ++g_nVRToken;
}

VR_INTERFACE uint32_t VR_CALLTYPE VR_InitInternal( EVRInitError *peError, EVRApplicationType eApplicationType );

uint32_t VR_InitInternal( EVRInitError *peError, vr::EVRApplicationType eApplicationType )
{
	return VR_InitInternal2( peError, eApplicationType, nullptr );
}

void VR_ShutdownInternal()
{
	std::lock_guard<std::recursive_mutex> lock( g_mutexSystem );

#if !defined( VR_API_PUBLIC )
	CleanupInternalInterfaces();
#endif

	if ( g_pHmdSystem )
	{
		g_pHmdSystem->Cleanup();
		g_pHmdSystem = NULL;
	}

	if ( g_pVRModule )
	{
		SharedLib_Unload( g_pVRModule );
		g_pVRModule = NULL;
	}

	++g_nVRToken;
}

EVRInitError VR_LoadHmdSystemInternal()
{
	std::string sRuntimePath, sConfigPath, sLogPath;

	bool bReadPathRegistry = CVRPathRegistry_Public::GetPaths( &sRuntimePath, &sConfigPath, &sLogPath, NULL, NULL );
	H2V_OpenVR_Trace( "path-registry-read" );
	if( !bReadPathRegistry )
	{
		return vr::VRInitError_Init_PathRegistryNotFound;
	}

	if( !Path_IsDirectory( sRuntimePath ) )
		return vr::VRInitError_Init_InstallationNotFound;

#if defined( WIN32 ) || defined( LINUX32 )
	std::string sBinPath = Path_Join( sRuntimePath, "bin" );
#else
	std::string sBinPath = Path_Join( sRuntimePath, "bin", PLATSUBDIR );
	#endif
	if( !Path_IsDirectory( sBinPath ) )
		return vr::VRInitError_Init_InstallationCorrupt;

#if defined( _WIN64 )
	std::string sClientPath = Path_Join( sBinPath, "vrclient_x64" DYNAMIC_LIB_EXT );
#else
	std::string sClientPath = Path_Join( sBinPath, "vrclient" DYNAMIC_LIB_EXT );
	#endif

	H2V_OpenVR_Trace( "client-library-load-enter" );
	H2V_OpenVR_TraceText( "client_path", sClientPath );
	#if defined( _WIN32 )
	const auto dependency_path = Path_Join( sRuntimePath, "bin", "win64" );
	H2V_OpenVR_TraceText( "dependency_path", dependency_path );
	H2V_OpenVR_DependencyDirectory dependency_directory( dependency_path );
	#endif
	std::string load_error;
	void *pMod = SharedLib_Load( sClientPath.c_str(), &load_error );
	H2V_OpenVR_Trace( "client-library-load-returned" );
	if ( pMod == nullptr ) H2V_OpenVR_TraceText( "client_load_error", load_error );
	// nothing more to do if we can't load the DLL
	if( !pMod )
	{
		return vr::VRInitError_Init_VRClientDLLNotFound;
	}

	VRClientCoreFactoryFn fnFactory = ( VRClientCoreFactoryFn )( SharedLib_GetFunction( pMod, "VRClientCoreFactory" ) );
	H2V_OpenVR_Trace( "client-core-factory-resolved" );
	if( !fnFactory )
	{
		SharedLib_Unload( pMod );
		return vr::VRInitError_Init_FactoryNotFound;
	}

	int nReturnCode = 0;
	g_pHmdSystem = static_cast< IVRClientCore * > ( fnFactory( vr::IVRClientCore_Version, &nReturnCode ) );
	H2V_OpenVR_Trace( "client-core-interface-created" );
	if( !g_pHmdSystem )
	{
		SharedLib_Unload( pMod );
		return vr::VRInitError_Init_InterfaceNotFound;
	}

	g_pVRModule = pMod;
	return VRInitError_None;
}


void *VR_GetGenericInterface(const char *pchInterfaceVersion, EVRInitError *peError)
{
	std::lock_guard<std::recursive_mutex> lock( g_mutexSystem );

	if (!g_pHmdSystem)
	{
		if (peError)
			*peError = vr::VRInitError_Init_NotInitialized;
		return NULL;
	}

	return g_pHmdSystem->GetGenericInterface(pchInterfaceVersion, peError);
}

bool VR_IsInterfaceVersionValid(const char *pchInterfaceVersion)
{
	std::lock_guard<std::recursive_mutex> lock( g_mutexSystem );

	if (!g_pHmdSystem)
	{
		return false;
	}

	return g_pHmdSystem->IsInterfaceVersionValid(pchInterfaceVersion) == VRInitError_None;
}

bool VR_IsHmdPresent()
{
	std::lock_guard<std::recursive_mutex> lock( g_mutexSystem );

	if( g_pHmdSystem )
	{
		// if we're already initialized, just call through
		return g_pHmdSystem->BIsHmdPresent();
	}
	else
	{
		// otherwise we need to do a bit more work
		EVRInitError err = VR_LoadHmdSystemInternal();
		if( err != VRInitError_None )
			return false;

		bool bHasHmd = g_pHmdSystem->BIsHmdPresent();

		g_pHmdSystem = NULL;
		SharedLib_Unload( g_pVRModule );
		g_pVRModule = NULL;

		return bHasHmd;
	}
}

/** Returns true if the OpenVR runtime is installed. */
bool VR_IsRuntimeInstalled()
{
	std::lock_guard<std::recursive_mutex> lock( g_mutexSystem );
	if( g_pHmdSystem )
	{
		// if we're already initialized, OpenVR is obviously installed
		return true;
	}
	else
	{
		// otherwise we need to do a bit more work
		std::string sRuntimePath, sConfigPath, sLogPath;

		bool bReadPathRegistry = CVRPathRegistry_Public::GetPaths( &sRuntimePath, &sConfigPath, &sLogPath, NULL, NULL );
		if( !bReadPathRegistry )
		{
			return false;
		}

		// figure out where we're going to look for vrclient.dll
		// see if the specified path actually exists.
		if( !Path_IsDirectory( sRuntimePath ) )
		{
			return false;
		}

		// the installation may be corrupt in some way, but it certainly looks installed
		return true;
	}
}


// -------------------------------------------------------------------------------
// Purpose: This is the old Runtime Path interface that is no longer exported in the
//			latest header. We still want to export it from the DLL, though, so updating
//			to a new DLL doesn't break old compiled code. This version was not thread 
//			safe and could change the buffer pointer to by a previous result on a 
//			subsequent call
// -------------------------------------------------------------------------------
VR_EXPORT_INTERFACE const char *VR_CALLTYPE VR_RuntimePath();

/** Returns where OpenVR runtime is installed. */
const char *VR_RuntimePath()
{
	static char rchBuffer[1024];
	uint32_t unRequiredSize;
	if ( VR_GetRuntimePath( rchBuffer, sizeof( rchBuffer ), &unRequiredSize ) && unRequiredSize < sizeof( rchBuffer ) )
	{
		return rchBuffer;
	}
	else
	{
		return nullptr;
	}
}


/** Returns where OpenVR runtime is installed. */
bool VR_GetRuntimePath( char *pchPathBuffer, uint32_t unBufferSize, uint32_t *punRequiredBufferSize )
{
	// otherwise we need to do a bit more work
	std::string sRuntimePath;

	*punRequiredBufferSize = 0;

	bool bReadPathRegistry = CVRPathRegistry_Public::GetPaths( &sRuntimePath, nullptr, nullptr, nullptr, nullptr );
	if ( !bReadPathRegistry )
	{
		return false;
	}

	// figure out where we're going to look for vrclient.dll
	// see if the specified path actually exists.
	if ( !Path_IsDirectory( sRuntimePath ) )
	{
		return false;
	}

	*punRequiredBufferSize = (uint32_t)sRuntimePath.size() + 1;
	if ( sRuntimePath.size() >= unBufferSize )
	{
		*pchPathBuffer = '\0';
	}
	else
	{
		strcpy_safe( pchPathBuffer, unBufferSize, sRuntimePath.c_str() );
	}

	return true;
}


/** Returns the symbol version of an HMD error. */
const char *VR_GetVRInitErrorAsSymbol( EVRInitError error )
{
	std::lock_guard<std::recursive_mutex> lock( g_mutexSystem );

	if( g_pHmdSystem )
		return g_pHmdSystem->GetIDForVRInitError( error );
	else
		return GetIDForVRInitError( error );
}


/** Returns the english string version of an HMD error. */
const char *VR_GetVRInitErrorAsEnglishDescription( EVRInitError error )
{
	std::lock_guard<std::recursive_mutex> lock( g_mutexSystem );

	if ( g_pHmdSystem )
		return g_pHmdSystem->GetEnglishStringForHmdError( error );
	else
		return GetEnglishStringForHmdError( error );
}


VR_INTERFACE const char *VR_CALLTYPE VR_GetStringForHmdError( vr::EVRInitError error );

/** Returns the english string version of an HMD error. */
const char *VR_GetStringForHmdError( EVRInitError error )
{
	return VR_GetVRInitErrorAsEnglishDescription( error );
}

}

