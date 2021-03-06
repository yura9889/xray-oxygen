// Build.cpp: implementation of the CBuild class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include "build.h"

#include "../xrLC_Light/xrMU_Model.h"



#include "../xrLC_Light/xrLC_GlobalData.h"
#include "../xrLC_Light/xrface.h"
#include "../xrLC_Light/mu_model_light.h"
#include "../xrLC_Light/net_cl_data_prepare.h"
#include "../xrLC_Light/serialize.h"
//#include "../xrLC_Light/lcnet_task_manager.h"
void	calc_ogf		( xrMU_Model &	mu_model );
void	export_geometry	( xrMU_Model &	mu_model );

void	export_ogf		( xrMU_Reference& mu_reference );

using namespace			std;
struct OGF_Base;
xr_vector<OGF_Base *>	g_tree;

//BOOL					b_noise		= FALSE;
//BOOL					b_radiosity	= FALSE;
//BOOL					b_net_light	= FALSE;
SBuildOptions			g_build_options;
vec2Face				g_XSplit;

void	CBuild::CheckBeforeSave( u32 stage )
{
	bool b_g_tree_empty = g_tree.empty() ;
	R_ASSERT( b_g_tree_empty );
	bool b_g_XSplit_empty = g_XSplit.empty();
	R_ASSERT( b_g_XSplit_empty );
	bool b_IsOGFContainersEmpty = IsOGFContainersEmpty();
	R_ASSERT( b_IsOGFContainersEmpty );
	
	
	
}

void	CBuild::TempSave( u32 stage )
{
	CheckBeforeSave( stage );

}

void	CBuild::read( INetReader &r )
{
	r_pod( r, g_build_options );

	r_pod( r, scene_bb );
	r_pod_vector( r, shader_render );
	r_pod_vector( r, shader_compile );
	r_pod_vector( r, L_dynamic );
	r_pod_vector( r, glows );
	r_pod_vector( r, portals );
	r_pod_vector( r, lods );
	r_pod( r, path );
	r_pod_vector( r, g_Shaders );
	
}
void	CBuild::write( IWriter	&w ) const 
{
	w_pod( w, g_build_options );
	
	w_pod( w, scene_bb );
	w_pod_vector( w, shader_render );
	w_pod_vector( w, shader_compile );
	w_pod_vector(  w,L_dynamic );
	w_pod_vector( w, glows );
	w_pod_vector( w, portals );
	w_pod_vector( w, lods );
	w_pod( w, path );
	w_pod_vector( w, g_Shaders );

}


//////////////////////////////////////////////////////////////////////

CBuild::CBuild()
{
	

}

CBuild::~CBuild()
{
	destroy_global_data();
}
 
CMemoryWriter&	CBuild::err_invalid()
{
	VERIFY(lc_global_data()); 
	return lc_global_data()->err_invalid(); 
}
CMemoryWriter&	CBuild::err_multiedge()
{
	VERIFY(lc_global_data()); 
	return lc_global_data()->err_multiedge(); 
}
CMemoryWriter	&CBuild::err_tjunction()
{
	VERIFY(lc_global_data()); 
	return lc_global_data()->err_tjunction(); 
}
xr_vector<b_material>&	CBuild::materials()	
{
	VERIFY(lc_global_data()); 
	return lc_global_data()->materials(); 
}
xr_vector<b_BuildTexture>&	CBuild::textures()		
{
	VERIFY(lc_global_data());
	return lc_global_data()->textures(); 
}

base_lighting&	CBuild::L_static()
{
	VERIFY(lc_global_data()); return lc_global_data()->L_static(); 
}

Shader_xrLC_LIB&	CBuild::shaders()		
{
	VERIFY(lc_global_data()); 
	return lc_global_data()->shaders(); 
}

extern u16		RegisterShader		(LPCSTR T);


void CBuild::Light_prepare()
{
	for (vecFaceIt I=lc_global_data()->g_faces().begin();	I!=lc_global_data()->g_faces().end(); I++) (*I)->CacheOpacity();
	for (u32 m=0; m<mu_models().size(); m++)	mu_models()[m]->calc_faceopacity();
}


//.#define CFORM_ONLY
#ifdef LOAD_GL_DATA
void net_light ();
#endif
void CBuild::Run	(LPCSTR P)
{
	lc_global_data()->initialize();
#ifdef LOAD_GL_DATA
	net_light ();
	return;
#endif
	//****************************************** Open Level
	strconcat					(sizeof(path),path,P,"\\")	;
	string_path					lfn				;
	IWriter* fs					= FS.w_open		(strconcat(sizeof(lfn),lfn,path,"level."));
	fs->open_chunk				(fsL_HEADER)	;
	hdrLEVEL H;	
	H.XRLC_version				= XRCL_PRODUCTION_VERSION;
	H.XRLC_quality				= g_params().m_quality;
	fs->w						(&H,sizeof(H));
	fs->close_chunk				();

	//****************************************** Dumb entry in shader-registration
	RegisterShader				("");

	//****************************************** Saving lights
	{
		string256			fn;
		IWriter*		fs	= FS.w_open	(strconcat(sizeof(fn),fn,pBuild->path,"build.lights"));
		fs->w_chunk			(0,&*L_static().rgb.begin(),L_static().rgb.size()*sizeof(R_Light));
		fs->w_chunk			(1,&*L_static().hemi.begin(),L_static().hemi.size()*sizeof(R_Light));
		fs->w_chunk			(2,&*L_static().sun.begin(),L_static().sun.size()*sizeof(R_Light));
		FS.w_close			(fs);
	}

	//****************************************** Optimizing + checking for T-junctions
	FPU::m64r					();
	Msg("\n %s", "Optimizing");
	mem_Compact					();
	PreOptimize					();
	CorrectTJunctions			();
	Phase("Optimizing");

	//****************************************** HEMI-Tesselate
	FPU::m64r					();
	Msg("%s", "Adaptive HT...");
	mem_Compact					();
#ifndef CFORM_ONLY
	xrPhase_AdaptiveHT			();
#endif
	Phase("Adaptive HT...");

	//****************************************** Building normals
	FPU::m64r					();
	Msg("%s", "Building normals...");
	mem_Compact					();
	CalcNormals					();
	Phase("Building normals...");

	//****************************************** Collision DB
	//should be after normals, so that double-sided faces gets separated
	FPU::m64r					();
	Msg("%s", "Building collision database...");
	mem_Compact					();
	BuildCForm					();
	Phase("Building collision database...");

#ifdef CFORM_ONLY
	return;
#endif

	BuildPortals				(*fs);

	//****************************************** T-Basis
	{
		FPU::m64r					();
		Msg("%s", "Building tangent-basis...");
		xrPhase_TangentBasis		();
		mem_Compact					();
		Phase("Building tangent-basis...");
	}

	//****************************************** GLOBAL-RayCast model
	FPU::m64r					();
	Msg("%s", "Building rcast-CFORM model...");
	mem_Compact					();
	Light_prepare				();
	BuildRapid					(TRUE);
	Phase("Building rcast-CFORM model...");

	//****************************************** GLOBAL-ILLUMINATION
	if (g_build_options.b_radiosity)			
	{
		FPU::m64r					();
		Msg("%s", "Radiosity-Solver...");
		mem_Compact					();
		Light_prepare				();
		xrPhase_Radiosity			();
		Phase("Radiosity-Solver...");
	}

	//****************************************** Starting MU
	FPU::m64r					();
	mem_Compact					();
	Light_prepare				();
	if(g_build_options.b_net_light)
	{
		lc_global_data()->mu_models_calc_materials();
		RunNetCompileDataPrepare( );
	}
	StartMu						();
	Phase("LIGHT: Starting MU...");
	//****************************************** Resolve materials
	FPU::m64r					();
	Msg("%s", "Resolving materials...");
	mem_Compact					();
	xrPhase_ResolveMaterials	();
	IsolateVertices				(TRUE);
	Phase("Resolving materials...");

	//****************************************** UV mapping
	{
		FPU::m64r					();
		Msg("%s", "Build UV mapping...");
		mem_Compact					();
		xrPhase_UVmap				();
		IsolateVertices				(TRUE);
		Phase("Build UV mapping...");
	}
	
	//****************************************** Subdivide geometry
	FPU::m64r					();
	Msg("%s", "Subdividing geometry...");
	mem_Compact					();
	xrPhase_Subdivide			();
	//IsolateVertices				(TRUE);
	lc_global_data()->vertices_isolate_and_pool_reload();
	Phase("Subdividing geometry...");
	//****************************************** All lighting + lmaps building and saving
#ifdef NET_CMP
	mu_base.wait				(500);
	mu_secondary.wait			(500);
#endif
	if(g_build_options.b_net_light)
		SetGlobalLightmapsDataInitialized();
		
	Light						();
	RunAfterLight				( fs );

}
void	CBuild::StartMu	()
{
  //mu_base.start				(xr_new<CMUThread> (0));
  run_mu_light( !!g_build_options.b_net_light );
}
void CBuild::	RunAfterLight			( IWriter* fs	)
{
 	//****************************************** Merge geometry
	FPU::m64r					();
	Msg("%s", "Merging geometry...");
	mem_Compact					();
	xrPhase_MergeGeometry		();
	Phase("Merging geometry...");

	//****************************************** Convert to OGF
	FPU::m64r					();
	Msg("%s", "Merging geometry...");
	mem_Compact					();
	Flex2OGF					();
	Phase("Converting to OGFs...");

	//****************************************** Wait for MU
	FPU::m64r					();
	Msg("%s", "LIGHT: Waiting for MU-thread...");
	mem_Compact					();
	wait_mu_base				();

	if( !g_build_options.b_net_light )
						wait_mu_secondary();
	Phase("LIGHT: Waiting for MU-thread...");
	//****************************************** Export MU-models
	FPU::m64r					();
	Msg("%s", "Converting MU-models to OGFs...");
	mem_Compact					();
	{
		u32 m;
		Status			("MU : Models...");
		for (m=0; m<mu_models().size(); m++)	{
			calc_ogf			(*mu_models()[m]);
			export_geometry		(*mu_models()[m]);
		}

		Status			("MU : References...");
		for (m=0; m<mu_refs().size(); m++)
			export_ogf(*mu_refs()[m]);

		Phase("Converting MU-models to OGFs...");
	}

	//****************************************** Destroy RCast-model
	FPU::m64r		();
	Msg("%s", "Destroying ray-trace model...");
	mem_Compact		();
	lc_global_data()->destroy_rcmodel();
	Phase("Destroying ray-trace model...");

	//****************************************** Build sectors
	FPU::m64r		();
	Msg("%s", "Building sectors...");
	mem_Compact		();
	BuildSectors	();
	Phase("Building sectors...");

	//****************************************** Saving MISC stuff
	FPU::m64r		();
	Msg("%s", "Saving...");
	mem_Compact		();
	SaveLights		(*fs);

	fs->open_chunk	(fsL_GLOWS);
	
	for (u32 i=0; i<glows.size(); i++)
	{
		b_glow&	G	= glows[i];
		fs->w		(&G,4*sizeof(float));
		string1024	sid;
		strconcat	(sizeof(sid),sid,
			shader_render[materials()[G.dwMaterial].shader].name,
			"/",
			textures()		[materials()[G.dwMaterial].surfidx].name
			);
		fs->w_u16	(RegisterShader(sid));
	}
	fs->close_chunk	();

	SaveTREE		(*fs);
	SaveSectors		(*fs);
	Phase("Saving...");
	err_save		();
}

void CBuild::err_save	()
{
	string_path		log_name;
	strconcat		(sizeof(log_name),log_name,"build_",Core.UserName,".err");
	FS.update_path	(log_name,"$logs$",log_name);

	IWriter*		fs	= FS.w_open(log_name);
	IWriter&		err = *fs;

	// t-junction
	err.open_chunk	(0);
	err.w_u32		(err_tjunction().size()/(1*sizeof(Fvector)));
	err.w			(err_tjunction().pointer(), err_tjunction().size());
	err.close_chunk	();

	// m-edje
	err.open_chunk	(1);
	err.w_u32		(err_multiedge().size()/(2*sizeof(Fvector)));
	err.w			(err_multiedge().pointer(), err_multiedge().size());
	err.close_chunk	();

	// invalid
	err.open_chunk	(2);
	err.w_u32		(err_invalid().size()/(3*sizeof(Fvector)));
	err.w			(err_invalid().pointer(), err_invalid().size());
	err.close_chunk	();

	FS.w_close( fs );
}

void CBuild::MU_ModelsCalculateNormals()
{
		for		(u32 m=0; m<mu_models().size(); m++)
			calc_normals( *mu_models()[m] );
}

xr_vector<xrMU_Model*>&CBuild::mu_models()
{
	VERIFY(lc_global_data()); 
	return lc_global_data()->mu_models(); 
}

xr_vector<xrMU_Reference*>&CBuild::mu_refs()
{
	VERIFY(lc_global_data()); 
	return lc_global_data()->mu_refs(); 
}

void CBuild::ImplicitLighting()
{
	::ImplicitLighting( g_build_options.b_net_light );
}