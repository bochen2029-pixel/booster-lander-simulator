#define _CRT_SECURE_NO_WARNINGS
#pragma warning(disable: 4996)
#include "setup.hpp"
#include <cstdlib>
#include <fstream>

// ============================================================================================
// KESTREL-9 BASE-FIRST DESCENT — D-060 (PLAN.md Phase 2.4): the frozen aero table in
// core/dynamics.c (CA(M), CN_alpha(M), Aref 10.52 m^2) checked against an LBM/LES computation of
// the operator's own CFD body (assets/kestrel9_gfx/exports/kestrel9_cfd_stowed_1-1_m.stl: legs
// STOWED = the aero-descent configuration; fins neutral; nozzles hollow to the throat plane).
//
// The lattice Boltzmann method is weakly compressible, so this checks the M -> 0 end of the table
// only: CA(0) = 0.85, CN_alpha(0) = 2.0 /rad. Flow at 30 m/s (M 0.09), air, Re_D ~ 7e6 with the
// Smagorinsky-Lilly subgrid model. The vehicle flies BASE FIRST: the octaweb faces upstream, the
// interstage is the trailing end. Required extensions in defines.hpp: FP16S, EQUILIBRIUM_BOUNDARIES,
// FORCE_FIELD, SUBGRID, GRAPHICS.
//
// Environment: K9_STL (path), K9_ALPHA_DEG (angle of attack, default 0), K9_VRAM_MB (default 4000),
// K9_FLOWTHROUGHS (default 3), K9_OUT (directory for frames + the force log).
// ============================================================================================

static float envf(const char* k, float d){ const char* v=getenv(k); return v? (float)atof(v) : d; }
static string envs(const char* k, const string& d){ const char* v=getenv(k); return v? string(v) : d; }

void main_setup() {
	const string stl = envs("K9_STL", "C:/Booster_Lander_Simulator/assets/kestrel9_gfx/exports/kestrel9_cfd_stowed_1-1_m.stl");
	const float alpha_deg = envf("K9_ALPHA_DEG", 0.0f);
	const uint vram_mb = (uint)envf("K9_VRAM_MB", 4000.0f);
	const float flowthroughs = envf("K9_FLOWTHROUGHS", 3.0f);
	const string out = envs("K9_OUT", "C:/Booster_Lander_Simulator/runs/d060/");

	// ---- box: 3.6 : 1 : 1, the vehicle's 49.4 m longest extent = 0.34 of the box length ----
	const uint3 lbm_N = resolution(float3(3.6f, 1.0f, 1.0f), vram_mb);
	const float si_len_vehicle = 47.7f + 1.66f;          // interstage top to bell lips, stowed export bbox
	const float si_u = 30.0f;                              // m/s, M 0.09
	const float si_nu = 1.48E-5f, si_rho = 1.225f;
	const float lbm_len_vehicle = 0.34f*(float)lbm_N.x;
	const float lbm_u = 0.075f;
	units.set_m_kg_s(lbm_len_vehicle, lbm_u, 1.0f, si_len_vehicle, si_u, si_rho);
	const float lbm_nu = units.nu(si_nu);
	const float si_dx = si_len_vehicle/lbm_len_vehicle;
	const float si_D = 3.66f, si_Aref = 10.52f;            // core/constants.h VEH_DIA, VEH_AREF
	const float lbm_R = 0.5f*si_D/si_dx;
	const float lbm_Aref = 3.14159265f*lbm_R*lbm_R;        // pi R^2 in cells^2 (== 10.52 m^2 scaled)
	const float q_lbm = 0.5f*1.0f*lbm_u*lbm_u;
	const float si_T = flowthroughs*((float)lbm_N.x*si_dx)/si_u;
	const ulong lbm_T = units.t(si_T);
	print_info("K9 CFD: N = "+to_string(lbm_N.x)+" x "+to_string(lbm_N.y)+" x "+to_string(lbm_N.z)+"  dx = "+to_string(si_dx, 4u)+" m  D = "+to_string(2.0f*lbm_R, 1u)+" cells");
	print_info("K9 CFD: Re_D = "+to_string(to_uint(units.si_Re(si_D, si_u, si_nu)))+"  lbm_nu = "+to_string(lbm_nu)+"  T = "+to_string(lbm_T)+" steps ("+to_string(si_T, 2u)+" s, "+to_string(flowthroughs, 1u)+" flow-throughs)  alpha = "+to_string(alpha_deg, 1u)+" deg");
	LBM lbm(lbm_N, 1u, 1u, 1u, lbm_nu);

	// ---- geometry: STL is Z-up with the body axis +Z; R_y(+90) turns +Z into +x (nose downstream,
	//      octaweb upstream = base-first); then R_z(alpha) tilts the nose toward +y ----
	const float3 center = float3(0.40f*(float)lbm_N.x, lbm.center().y, lbm.center().z);
	const float3x3 rotation = float3x3(float3(0, 0, 1), radians(alpha_deg))*float3x3(float3(0, 1, 0), radians(90.0f));
	lbm.voxelize_stl(stl, center, rotation, lbm_len_vehicle);
	const uint Nx=lbm.get_Nx(), Ny=lbm.get_Ny(), Nz=lbm.get_Nz();
	// START FROM REST + RAMP THE INFLOW. An impulsive start (u = u_in everywhere at t = 0) fills the
	// closed box with an acoustic standing wave whose pressure gradient across the 49 m body swung
	// the measured CA between -5.6 and +8.5 with the box's round-trip period (~3200 steps at
	// c_s = 1/sqrt(3)) and barely decays at this viscosity. Instead: fluid at rest, the free-stream
	// velocity on the TYPE_E faces raised with a raised-cosine over ~1/6 of the run (2.5 round trips).
	parallel_for(lbm.get_N(), [&](ulong n) { uint x=0u, y=0u, z=0u; lbm.coordinates(n, x, y, z);
		if(x==0u||x==Nx-1u||y==0u||y==Ny-1u||z==0u||z==Nz-1u) lbm.flags[n] = TYPE_E; // free stream on every face
	});
	const ulong T_ramp = lbm_T/6ul;
	auto set_inflow = [&](float u_now) {
		parallel_for(lbm.get_N(), [&](ulong n) { if(lbm.flags[n]==TYPE_E) lbm.u.x[n] = u_now; });
		lbm.u.write_to_device();
	};

	// ---- run: sample the force on the vehicle every 500 steps; average the last third ----
	const float a_dir[3] = { cosf(radians(alpha_deg)), sinf(radians(alpha_deg)), 0.0f };   // body axis, base -> nose
	const float c_dir[3] = { sinf(radians(alpha_deg)), -cosf(radians(alpha_deg)), 0.0f };  // crossflow direction (normal force acts along it)
	std::ofstream log((out+"force_alpha"+to_string(alpha_deg, 0u)+".csv").c_str());
	log << "step,t_si,Fx,Fy,Fz,CA,CN\n";
	double sumCA=0.0, sumCN=0.0; ulong nsum=0ul;
	lbm.graphics.visualization_modes = VIS_FLAG_SURFACE|VIS_Q_CRITERION;
	lbm.run(0u, lbm_T);
	lbm.write_status();
	ulong next_frame = 0ul; bool ramp_done = false;
	while(lbm.get_t()<=lbm_T) {
		{ const ulong tt = lbm.get_t();
		  if(tt < T_ramp){ const float f = 0.5f-0.5f*cosf(3.14159265f*(float)tt/(float)T_ramp); set_inflow(lbm_u*f); }
		  else if(!ramp_done){ set_inflow(lbm_u); ramp_done = true; } }
		lbm.run(500u, lbm_T);
		lbm.update_force_field();
		const float3 F = lbm.object_force(TYPE_S);
		const float Fa = F.x*a_dir[0]+F.y*a_dir[1]+F.z*a_dir[2];
		const float Fc = F.x*c_dir[0]+F.y*c_dir[1]+F.z*c_dir[2];
		const float CA = Fa/(q_lbm*lbm_Aref), CN = Fc/(q_lbm*lbm_Aref);
		const ulong t = lbm.get_t();
		if(t >= (3ul*lbm_T)/5ul){ sumCA+=CA; sumCN+=CN; nsum++; }   /* average the last 40% */
		log << t << "," << units.si_t(t) << "," << F.x << "," << F.y << "," << F.z << "," << CA << "," << CN << "\n"; log.flush();
		if(t%2500ul==0ul) print_info("K9 CFD t="+to_string(t)+"/"+to_string(lbm_T)+"  CA="+to_string(CA, 3u)+"  CN="+to_string(CN, 3u)+"  |F|si="+to_string(units.si_F(length(F)), 0u)+" N");
#if defined(GRAPHICS) && !defined(INTERACTIVE_GRAPHICS)
		if(t >= next_frame){
			lbm.graphics.set_camera_free(float3(0.45f*(float)Nx, -0.9f*(float)Ny, 0.5f*(float)Nz), -90.0f, 10.0f, 30.0f);
			lbm.graphics.write_frame(out+"frames/side_alpha"+to_string(alpha_deg, 0u)+"/");
			next_frame = t + lbm_T/8ul;
		}
#endif
	}
	lbm.write_status();
	const double CAm = nsum? sumCA/(double)nsum : 0.0, CNm = nsum? sumCN/(double)nsum : 0.0;
	print_info("K9 CFD RESULT alpha="+to_string(alpha_deg, 1u)+" deg: CA = "+to_string((float)CAm, 3u)+"  CN = "+to_string((float)CNm, 3u)+"  (mean over last 40%, "+to_string(nsum)+" samples; table: CA(0)=0.85, CN=2.0*alpha_rad="+to_string(2.0f*radians(alpha_deg), 3u)+")");
	log << "# RESULT alpha_deg=" << alpha_deg << " CA=" << CAm << " CN=" << CNm << " samples=" << nsum << "\n";
	log.close();
}
