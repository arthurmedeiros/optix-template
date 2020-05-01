// ======================================================================== //
// Copyright 2018-2019 Ingo Wald                                            //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

#include <optix_device.h>

#include "LaunchParams.h"

using namespace osc;

namespace osc {
  
  /*! launch parameters in constant memory, filled in by optix upon
      optixLaunch (this gets filled in from the buffer we pass to
      optixLaunch) */
  extern "C" __constant__ LaunchParams optixLaunchParams;

  __device__ vec3f lightPos;
  
  static __forceinline__ __device__
  void *unpackPointer( uint32_t i0, uint32_t i1 )
  {
    const uint64_t uptr = static_cast<uint64_t>( i0 ) << 32 | i1;
    void*           ptr = reinterpret_cast<void*>( uptr ); 
    return ptr;
  }

  static __forceinline__ __device__
  void  packPointer( void* ptr, uint32_t& i0, uint32_t& i1 )
  {
    const uint64_t uptr = reinterpret_cast<uint64_t>( ptr );
    i0 = uptr >> 32;
    i1 = uptr & 0x00000000ffffffff;
  }

  template<typename T>
  static __forceinline__ __device__ T* getPRD()
  {
      const uint32_t u0 = optixGetPayload_0();
      const uint32_t u1 = optixGetPayload_1();
      return reinterpret_cast<T*>(unpackPointer(u0, u1));
  }

  template<typename T>
  static __forceinline__ __device__ T* getHitNormal()
  {
      const uint32_t u2 = optixGetPayload_2();
      const uint32_t u3 = optixGetPayload_3();
      return reinterpret_cast<T*>(unpackPointer(u2, u3));
  }
  
  //------------------------------------------------------------------------------
  // closest hit and anyhit programs for radiance-type rays.
  //
  // Note eventually we will have to create one pair of those for each
  // ray type and each geometry type we want to render; but this
  // simple example doesn't use any actual geometries yet, so we only
  // create a single, dummy, set of them (we do have to have at least
  // one group of them to set up the SBT)
  //------------------------------------------------------------------------------
  
  extern "C" __global__ void __closesthit__empty() {

  }


  extern "C" __global__ void __closesthit__radiance_mesh()
  {
      const GeometrySBTData& geometrySbtData
          = *(const GeometrySBTData*)optixGetSbtDataPointer();

      vec3f normal, color;
      const TriangleMeshSBTData sbtData = geometrySbtData.triangle_data;
      // compute normal:
      const int   primID = optixGetPrimitiveIndex();
      const vec3i index = sbtData.index[primID];
      const vec3f& A = sbtData.vertex[index.x];
      const vec3f& B = sbtData.vertex[index.y];
      const vec3f& C = sbtData.vertex[index.z];
      normal = normalize(cross(C - A, B - A));
      color = sbtData.color;
      const float u = optixGetTriangleBarycentrics().x;
      const float v = optixGetTriangleBarycentrics().y;

      const vec3f pos = (1.f - u - v) * sbtData.vertex[index.x]
          + u * sbtData.vertex[index.y]
          + v * sbtData.vertex[index.z];
      vec3f lightDir = lightPos-pos;
      float tempcos = dot(normalize(lightDir), normal);
      tempcos = tempcos > 0 ? tempcos : 0;
      vec3f& prd = *(vec3f*)getPRD<vec3f>();

      vec3f lightVisibility = vec3f(1.0f);

      uint32_t u0, u1;
      packPointer(&lightVisibility, u0, u1);

      optixTrace(optixLaunchParams.traversable,
          pos,
          normalize(lightDir),
          1e-3f,    // tmin
          length(lightDir),  // tmax
          0.0f,   // rayTime
          OptixVisibilityMask(255),
          OPTIX_RAY_FLAG_NONE,//OPTIX_RAY_FLAG_NONE,
          SHADOW_RAY_TYPE,             // SBT offset
          RAY_TYPE_COUNT,               // SBT stride
          SHADOW_RAY_TYPE,             // missSBTIndex 
          u0, u1);
      prd = (0.2f + 0.8f * tempcos * lightVisibility) * color;
  }

  extern "C" __global__ void __closesthit__radiance_sphere()
  {
      const GeometrySBTData& geometrySbtData
          = *(const GeometrySBTData*)optixGetSbtDataPointer();

      vec3f normal, color;
      const SphereSBTData sbtData = geometrySbtData.sphere_data;
      normal = *(vec3f*)getHitNormal<vec3f>();
      color = sbtData.color;
      vec3f rayOrigin = optixGetWorldRayOrigin();
      vec3f rayDirection = optixGetWorldRayDirection();
      vec3f pos = sbtData.center + normal * sbtData.radius;
      vec3f lightDir = lightPos - pos;
      float tempcos = dot(normalize(lightDir), normal);
      tempcos = tempcos > 0 ? tempcos : 0;
      const float cosDN = 0.2f + .8f * tempcos;
      vec3f& prd = *(vec3f*)getPRD<vec3f>();

      vec3f lightVisibility = vec3f(1.0f);

      uint32_t u0, u1;
      packPointer(&lightVisibility, u0, u1);

      optixTrace(optixLaunchParams.traversable,
          pos,
          normalize(lightDir),
          1e-3f,    // tmin
          length(lightDir),  // tmax
          0.0f,   // rayTime
          OptixVisibilityMask(255),
          OPTIX_RAY_FLAG_NONE,//OPTIX_RAY_FLAG_NONE,
          SHADOW_RAY_TYPE,             // SBT offset
          RAY_TYPE_COUNT,               // SBT stride
          SHADOW_RAY_TYPE,             // missSBTIndex 
          u0, u1);

      prd = (0.2f + 0.8f * cosDN * lightVisibility) * color;
  }
  
  extern "C" __global__ void __anyhit__empty()
  { /*! for this simple example, this will remain empty */ }

  extern "C" __global__ void __anyhit__shadow()
  { 
      *getPRD<vec3f>() = vec3f(0.f);
      optixTerminateRay();
  }

  extern "C" __global__ void __intersection__empty() {
  }

  extern "C" __global__ void __intersection__sphere()
  {
      const GeometrySBTData& geometrySbtData
          = *(const GeometrySBTData*)optixGetSbtDataPointer();
      const SphereSBTData sbtData = geometrySbtData.sphere_data;

      const vec3f orig = optixGetWorldRayOrigin();
      const vec3f dir = optixGetWorldRayDirection();

      const vec3f center = sbtData.center;
      const float  radius = sbtData.radius;
      const vec3f O = orig - center;
      const float  l = 1 / length(dir);
      const vec3f D = dir * l;

      const float b = dot(O, D);
      const float c = dot(O, O) - radius * radius;
      const float disc = b * b - c;
      if (disc > 0.0f)
      {
          const float sdisc = sqrtf(disc);
          const float root1 = (-b - sdisc);
          const float        root11 = 0.0f;
          const vec3f       shading_normal = (O + (root1 + root11) * D) / radius;
          vec3f& normal = *(vec3f*)getHitNormal<vec3f>();
          normal = normalize(shading_normal);

          //TODO: passa a normal, burro

          optixReportIntersection(
              root1,      // t hit
              0,          // user hit kind
              optixGetPayload_0(), optixGetPayload_1(), optixGetPayload_2(), optixGetPayload_3()
          );
      }
  }
  
  //------------------------------------------------------------------------------
  // miss program that gets called for any ray that did not have a
  // valid intersection
  //
  // as with the anyhit/closest hit programs, in this example we only
  // need to have _some_ dummy function to set up a valid SBT
  // ------------------------------------------------------------------------------
  
  extern "C" __global__ void __miss__empty()
  {
  }

  extern "C" __global__ void __miss__radiance()
  {
    vec3f &prd = *(vec3f*)getPRD<vec3f>();

    const vec3f rayDir = optixGetWorldRayDirection();

    const vec3f color1 = vec3f(1.0f, 1.0f, 1.0f);
    const vec3f color2 = vec3f(0.8f, 0.0f, 0.8f);
    float t = 0.5f*(rayDir.y + 1.0f); 

    // set to constant white as background color
    prd = t*color2 + (1-t)*color1;
  }

  //------------------------------------------------------------------------------
  // ray gen program - the actual rendering happens in here
  //------------------------------------------------------------------------------
  extern "C" __global__ void __raygen__renderFrame()
  {
    // compute a test pattern based on pixel ID
    const int ix = optixGetLaunchIndex().x;
    const int iy = optixGetLaunchIndex().y;

    const auto &camera = optixLaunchParams.camera;

    // our per-ray data for this example. what we initialize it to
    // won't matter, since this value will be overwritten by either
    // the miss or hit program, anyway
    vec3f pixelColorPRD = vec3f(0.f);

    vec3f hitNormal = vec3f(0.f);

    // the values we store the PRD pointer in:
    uint32_t u0, u1, u2, u3;
    packPointer(&pixelColorPRD, u0, u1);

    packPointer(&hitNormal, u2, u3);

    // normalized screen plane position, in [0,1]^2
    const vec2f screen(vec2f(ix+.5f,iy+.5f)
                       / vec2f(optixLaunchParams.frame.size));
    
    // generate ray direction
    vec3f rayDir = normalize(camera.direction
                             + (screen.x - 0.5f) * camera.horizontal
                             + (screen.y - 0.5f) * camera.vertical);

    lightPos = vec3f(0.0f, 3.0f, 0.0f);

    optixTrace(optixLaunchParams.traversable,
               camera.position,
               rayDir,
               0.f,    // tmin
               1e20f,  // tmax
               0.0f,   // rayTime
               OptixVisibilityMask( 255 ),
               OPTIX_RAY_FLAG_DISABLE_ANYHIT,//OPTIX_RAY_FLAG_NONE,
               SURFACE_RAY_TYPE,             // SBT offset
               RAY_TYPE_COUNT,               // SBT stride
               SURFACE_RAY_TYPE,             // missSBTIndex 
               u0, u1, u2, u3 );

    const int r = int(255.99f*pixelColorPRD.x);
    const int g = int(255.99f*pixelColorPRD.y);
    const int b = int(255.99f*pixelColorPRD.z);

    // convert to 32-bit rgba value (we explicitly set alpha to 0xff
    // to make stb_image_write happy ...
    const uint32_t rgba = 0xff000000
      | (r<<0) | (g<<8) | (b<<16);

    // and write to frame buffer ...
    const uint32_t fbIndex = ix+iy*optixLaunchParams.frame.size.x;
    optixLaunchParams.frame.colorBuffer[fbIndex] = rgba;
  }
  
} // ::osc
