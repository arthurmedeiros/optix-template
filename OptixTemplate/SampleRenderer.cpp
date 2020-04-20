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

#include "SampleRenderer.h"
// this include may only appear in a single source file:
#include <optix_function_table_definition.h>

/*! \namespace osc - Optix Siggraph Course */
namespace osc {

  extern "C" char embedded_ptx_code[];

  /*! SBT record for a raygen program */
  struct __align__( OPTIX_SBT_RECORD_ALIGNMENT ) RaygenRecord
  {
    __align__( OPTIX_SBT_RECORD_ALIGNMENT ) char header[OPTIX_SBT_RECORD_HEADER_SIZE];
    // just a dummy value - later examples will use more interesting
    // data here
    void *data;
  };

  /*! SBT record for a miss program */
  struct __align__( OPTIX_SBT_RECORD_ALIGNMENT ) MissRecord
  {
    __align__( OPTIX_SBT_RECORD_ALIGNMENT ) char header[OPTIX_SBT_RECORD_HEADER_SIZE];
    // just a dummy value - later examples will use more interesting
    // data here
    void *data;
  };

  /*! SBT record for a hitgroup program */
  struct __align__( OPTIX_SBT_RECORD_ALIGNMENT ) HitgroupRecord
  {
    __align__( OPTIX_SBT_RECORD_ALIGNMENT ) char header[OPTIX_SBT_RECORD_HEADER_SIZE];
    GeometrySBTData data;
  };


  //! add aligned cube with front-lower-left corner and size
  void TriangleMesh::addCube(const vec3f &center, const vec3f &size)
  {
    PING; 
    affine3f xfm;
    xfm.p = center - 0.5f*size;
    xfm.l.vx = vec3f(size.x,0.f,0.f);
    xfm.l.vy = vec3f(0.f,size.y,0.f);
    xfm.l.vz = vec3f(0.f,0.f,size.z);
    addUnitCube(xfm);
  }
  
  /*! add a unit cube (subject to given xfm matrix) to the current
      triangleMesh */
  void TriangleMesh::addUnitCube(const affine3f &xfm)
  {
    int firstVertexID = (int)vertex.size();
    vertex.push_back(xfmPoint(xfm,vec3f(0.f,0.f,0.f)));
    vertex.push_back(xfmPoint(xfm,vec3f(1.f,0.f,0.f)));
    vertex.push_back(xfmPoint(xfm,vec3f(0.f,1.f,0.f)));
    vertex.push_back(xfmPoint(xfm,vec3f(1.f,1.f,0.f)));
    vertex.push_back(xfmPoint(xfm,vec3f(0.f,0.f,1.f)));
    vertex.push_back(xfmPoint(xfm,vec3f(1.f,0.f,1.f)));
    vertex.push_back(xfmPoint(xfm,vec3f(0.f,1.f,1.f)));
    vertex.push_back(xfmPoint(xfm,vec3f(1.f,1.f,1.f)));


    int indices[] = {0,1,3, 2,3,0,
                     5,7,6, 5,6,4,
                     0,4,5, 0,5,1,
                     2,3,7, 2,7,6,
                     1,5,7, 1,7,3,
                     4,0,2, 4,2,6
                     };
    for (int i=0;i<12;i++)
      index.push_back(firstVertexID+vec3i(indices[3*i+0],
                                          indices[3*i+1],
                                          indices[3*i+2]));
  }
    
  void Sphere::addSphere(const float r, const vec3f col) {
      radius = r;
      color = col;
  }
  
  /*! constructor - performs all setup, including initializing
    optix, creates module, pipeline, programs, SBT, etc. */
  SampleRenderer::SampleRenderer(const Geometry &scene)
    : scene(scene)
  {
    initOptix();
      
    std::cout << "#osc: creating optix context ..." << std::endl;
    createContext();
      
    std::cout << "#osc: setting up module ..." << std::endl;
    createModule();

    std::cout << "#osc: creating raygen programs ..." << std::endl;
    createRaygenPrograms();
    std::cout << "#osc: creating miss programs ..." << std::endl;
    createMissPrograms();
    std::cout << "#osc: creating hitgroup programs ..." << std::endl;
    createHitgroupPrograms();

    OptixTraversableHandle meshesGAS = buildAccelMeshes();
    OptixTraversableHandle spheresGAS = buildAccelSpheres();
    OptixTraversableHandle sceneTAS = buildAccelInstances(meshesGAS, spheresGAS);
    launchParams.traversable = sceneTAS;
    
    std::cout << "#osc: setting up optix pipeline ..." << std::endl;
    createPipeline();

    std::cout << "#osc: building SBT ..." << std::endl;
    buildSBT();

    launchParamsBuffer.alloc(sizeof(launchParams));
    std::cout << "#osc: context, module, pipeline, etc, all set up ..." << std::endl;

    std::cout << GDT_TERMINAL_GREEN;
    std::cout << "#osc: Optix 7 Sample fully set up" << std::endl;
    std::cout << GDT_TERMINAL_DEFAULT;
  }

  OptixTraversableHandle SampleRenderer::buildAccelMeshes()
  {
    vertexBuffer.resize(scene.meshes.size());
    indexBuffer.resize(scene.meshes.size());
    
    OptixTraversableHandle asHandle { 0 };
    
    // ==================================================================
    // triangle inputs
    // ==================================================================
	std::vector<OptixBuildInput> geometryInput(scene.meshes.size());
    std::vector<CUdeviceptr> d_vertices(scene.meshes.size());
	std::vector<CUdeviceptr> d_indices(scene.meshes.size());
	std::vector<uint32_t> geometryInputFlags(scene.meshes.size());

    for (int meshID=0;meshID< scene.meshes.size();meshID++) {
        TriangleMesh& mesh = scene.meshes[meshID];
        // upload the model to the device: the builder
        vertexBuffer[meshID].alloc_and_upload(mesh.vertex);
        indexBuffer[meshID].alloc_and_upload(mesh.index);

        geometryInput[meshID] = {};
        geometryInput[meshID].type
            = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;

        // create local variables, because we need a *pointer* to the
        // device pointers
        d_vertices[meshID] = vertexBuffer[meshID].d_pointer();
        d_indices[meshID] = indexBuffer[meshID].d_pointer();

        geometryInput[meshID].triangleArray.vertexFormat = OPTIX_VERTEX_FORMAT_FLOAT3;
        geometryInput[meshID].triangleArray.vertexStrideInBytes = sizeof(vec3f);
        geometryInput[meshID].triangleArray.numVertices = (int)mesh.vertex.size();
        geometryInput[meshID].triangleArray.vertexBuffers = &d_vertices[meshID];

        geometryInput[meshID].triangleArray.indexFormat = OPTIX_INDICES_FORMAT_UNSIGNED_INT3;
        geometryInput[meshID].triangleArray.indexStrideInBytes = sizeof(vec3i);
        geometryInput[meshID].triangleArray.numIndexTriplets = (int)mesh.index.size();
        geometryInput[meshID].triangleArray.indexBuffer = d_indices[meshID];

        geometryInputFlags[meshID] = OPTIX_GEOMETRY_FLAG_NONE;

        // in this example we have one SBT entry, and no per-primitive
        // materials:
        geometryInput[meshID].triangleArray.flags = &geometryInputFlags[meshID];
        geometryInput[meshID].triangleArray.numSbtRecords = 1;
        geometryInput[meshID].triangleArray.sbtIndexOffsetBuffer = 0;
        geometryInput[meshID].triangleArray.sbtIndexOffsetSizeInBytes = 0;
        geometryInput[meshID].triangleArray.sbtIndexOffsetStrideInBytes = 0;
    }
    // ==================================================================
    // BLAS setup
    // ==================================================================
    
    OptixAccelBuildOptions accelOptions = {};
    accelOptions.buildFlags             = OPTIX_BUILD_FLAG_NONE
      | OPTIX_BUILD_FLAG_ALLOW_COMPACTION
      ;
    accelOptions.motionOptions.numKeys  = 1;
    accelOptions.operation              = OPTIX_BUILD_OPERATION_BUILD;
    
    OptixAccelBufferSizes blasBufferSizes;
    OPTIX_CHECK(optixAccelComputeMemoryUsage
                (optixContext,
                 &accelOptions,
                 geometryInput.data(),
                 (int)scene.meshes.size(),  // num_build_inputs
                 &blasBufferSizes
                 ));
    
    // ==================================================================
    // prepare compaction
    // ==================================================================
    
    CUDABuffer compactedSizeBuffer;
    compactedSizeBuffer.alloc(sizeof(uint64_t));
    
    OptixAccelEmitDesc emitDesc;
    emitDesc.type   = OPTIX_PROPERTY_TYPE_COMPACTED_SIZE;
    emitDesc.result = compactedSizeBuffer.d_pointer();
    
    // ==================================================================
    // execute build (main stage)
    // ==================================================================
    
    CUDABuffer tempBuffer;
    tempBuffer.alloc(blasBufferSizes.tempSizeInBytes);
    
    CUDABuffer outputBuffer;
    outputBuffer.alloc(blasBufferSizes.outputSizeInBytes);
      
    OPTIX_CHECK(optixAccelBuild(optixContext,
                                /* stream */0,
                                &accelOptions,
                                geometryInput.data(),
                                (int)scene.meshes.size(),
                                tempBuffer.d_pointer(),
                                tempBuffer.sizeInBytes,
                                
                                outputBuffer.d_pointer(),
                                outputBuffer.sizeInBytes,
                                
                                &asHandle,
                                
                                &emitDesc,1
                                ));
    CUDA_SYNC_CHECK();
    
    // ==================================================================
    // perform compaction
    // ==================================================================
    uint64_t compactedSize;
    compactedSizeBuffer.download(&compactedSize,1);
    
    meshBlasBuffer.alloc(compactedSize);
    OPTIX_CHECK(optixAccelCompact(optixContext,
                                  /*stream:*/0,
                                  asHandle,
                                  meshBlasBuffer.d_pointer(),
                                  meshBlasBuffer.sizeInBytes,
                                  &asHandle));
    CUDA_SYNC_CHECK();
    
    // ==================================================================
    // aaaaaand .... clean up
    // ==================================================================
    outputBuffer.free(); // << the UNcompacted, temporary output buffer
    tempBuffer.free();
    compactedSizeBuffer.free();
    
    return asHandle;
  }


  OptixTraversableHandle SampleRenderer::buildAccelSpheres()
  {
      aabbBuffer.resize(scene.spheres.size());

      OptixTraversableHandle asHandle{ 0 };

      // ==================================================================
      // triangle inputs
      // ==================================================================
      std::vector<OptixBuildInput> geometryInput(scene.spheres.size());
      std::vector<CUdeviceptr> d_vertices(scene.spheres.size());
      std::vector<CUdeviceptr> d_indices(scene.spheres.size());
      std::vector<uint32_t> geometryInputFlags(scene.spheres.size());

      for (int sphereID = 0; sphereID < scene.spheres.size(); sphereID++) {
            Sphere& model = scene.spheres[sphereID];
            OptixAabb   aabb = { -model.radius, -model.radius, -model.radius, model.radius, model.radius, model.radius };
            aabbBuffer[sphereID].alloc_and_upload(aabb);

            geometryInput[sphereID] = {};
            geometryInput[sphereID].type
                = OPTIX_BUILD_INPUT_TYPE_CUSTOM_PRIMITIVES;

            d_vertices[sphereID] = aabbBuffer[sphereID].d_pointer();

            geometryInput[sphereID].aabbArray.aabbBuffers = &d_vertices[sphereID];
            geometryInput[sphereID].aabbArray.numPrimitives = 1;
            geometryInput[sphereID].aabbArray.strideInBytes = 0;


            geometryInputFlags[sphereID] = OPTIX_GEOMETRY_FLAG_NONE;

            geometryInput[sphereID].aabbArray.flags = &geometryInputFlags[sphereID];
            geometryInput[sphereID].aabbArray.numSbtRecords = 1;
            geometryInput[sphereID].aabbArray.sbtIndexOffsetBuffer = 0;
            geometryInput[sphereID].aabbArray.sbtIndexOffsetSizeInBytes = 0;
            geometryInput[sphereID].aabbArray.sbtIndexOffsetStrideInBytes = 0;
            geometryInput[sphereID].aabbArray.primitiveIndexOffset = 0;
      }
      // ==================================================================
      // BLAS setup
      // ==================================================================

      OptixAccelBuildOptions accelOptions = {};
      accelOptions.buildFlags = OPTIX_BUILD_FLAG_NONE
          | OPTIX_BUILD_FLAG_ALLOW_COMPACTION
          ;
      accelOptions.motionOptions.numKeys = 1;
      accelOptions.operation = OPTIX_BUILD_OPERATION_BUILD;

      OptixAccelBufferSizes blasBufferSizes;
      OPTIX_CHECK(optixAccelComputeMemoryUsage
      (optixContext,
          &accelOptions,
          geometryInput.data(),
          (int)scene.meshes.size(),  // num_build_inputs
          &blasBufferSizes
      ));

      // ==================================================================
      // prepare compaction
      // ==================================================================

      CUDABuffer compactedSizeBuffer;
      compactedSizeBuffer.alloc(sizeof(uint64_t));

      OptixAccelEmitDesc emitDesc;
      emitDesc.type = OPTIX_PROPERTY_TYPE_COMPACTED_SIZE;
      emitDesc.result = compactedSizeBuffer.d_pointer();

      // ==================================================================
      // execute build (main stage)
      // ==================================================================

      CUDABuffer tempBuffer;
      tempBuffer.alloc(blasBufferSizes.tempSizeInBytes);

      CUDABuffer outputBuffer;
      outputBuffer.alloc(blasBufferSizes.outputSizeInBytes);

      OPTIX_CHECK(optixAccelBuild(optixContext,
          /* stream */0,
          &accelOptions,
          geometryInput.data(),
          (int)scene.meshes.size(),
          tempBuffer.d_pointer(),
          tempBuffer.sizeInBytes,

          outputBuffer.d_pointer(),
          outputBuffer.sizeInBytes,

          &asHandle,

          &emitDesc, 1
      ));
      CUDA_SYNC_CHECK();

      // ==================================================================
      // perform compaction
      // ==================================================================
      uint64_t compactedSize;
      compactedSizeBuffer.download(&compactedSize, 1);

      sphereBlasBuffer.alloc(compactedSize);
      OPTIX_CHECK(optixAccelCompact(optixContext,
          /*stream:*/0,
          asHandle,
          sphereBlasBuffer.d_pointer(),
          sphereBlasBuffer.sizeInBytes,
          &asHandle));
      CUDA_SYNC_CHECK();

      // ==================================================================
      // aaaaaand .... clean up
      // ==================================================================
      outputBuffer.free(); // << the UNcompacted, temporary output buffer
      tempBuffer.free();
      compactedSizeBuffer.free();

      return asHandle;
  }

  OptixTraversableHandle SampleRenderer::buildAccelInstances(OptixTraversableHandle meshes, OptixTraversableHandle spheres)
  {
      OptixTraversableHandle asHandle{ 0 };

      OptixBuildInput buildInput = {};
      buildInput.type = OPTIX_BUILD_INPUT_TYPE_INSTANCES;

      float transform[12] = { 1, 0, 0, 0,
                             0, 1, 0, 0,
                             0, 0, 1, 0 };

      std::vector<OptixInstance> instances;

      OptixInstance meshInstance;
      memcpy(meshInstance.transform, transform, sizeof(float) * 12);
      meshInstance.instanceId = 0;
      meshInstance.visibilityMask = 255;
      meshInstance.sbtOffset = 0;
      meshInstance.flags = OPTIX_INSTANCE_FLAG_NONE;
      meshInstance.traversableHandle = meshes;

      instances.push_back(meshInstance);

      OptixInstance sphereInstance;
      memcpy(sphereInstance.transform, transform, sizeof(float) * 12);
      sphereInstance.instanceId = 1;
      sphereInstance.visibilityMask = 255;
      sphereInstance.sbtOffset = (uint32_t) scene.meshes.size();
      sphereInstance.flags = OPTIX_INSTANCE_FLAG_NONE;
      sphereInstance.traversableHandle = spheres;

      instances.push_back(sphereInstance);

      CUDABuffer instanceBuffer;
      instanceBuffer.alloc_and_upload(instances);

      buildInput.instanceArray.instances = instanceBuffer.d_pointer();
      buildInput.instanceArray.numInstances = 2;
      buildInput.instanceArray.aabbs = 0;
      buildInput.instanceArray.numAabbs = 0;

      OptixAccelBuildOptions accelOptions = {};
      accelOptions.buildFlags = OPTIX_BUILD_FLAG_NONE
          | OPTIX_BUILD_FLAG_ALLOW_COMPACTION
          ;
      accelOptions.motionOptions.numKeys = 1;
      accelOptions.operation = OPTIX_BUILD_OPERATION_BUILD;

      OptixAccelBufferSizes tlasBufferSizes;
      OPTIX_CHECK(optixAccelComputeMemoryUsage
      (optixContext,
          &accelOptions,
          &buildInput,
          1,
          &tlasBufferSizes
      ));

      // ==================================================================
      // prepare compaction
      // ==================================================================

      CUDABuffer compactedSizeBuffer;
      compactedSizeBuffer.alloc(sizeof(uint64_t));

      OptixAccelEmitDesc emitDesc;
      emitDesc.type = OPTIX_PROPERTY_TYPE_COMPACTED_SIZE;
      emitDesc.result = compactedSizeBuffer.d_pointer();

      // ==================================================================
      // execute build (main stage)
      // ==================================================================

      CUDABuffer tempBuffer;
      tempBuffer.alloc(tlasBufferSizes.tempSizeInBytes);

      CUDABuffer outputBuffer;
      outputBuffer.alloc(tlasBufferSizes.outputSizeInBytes);

      OPTIX_CHECK(optixAccelBuild(optixContext,
          /* stream */0,
          &accelOptions,
          &buildInput,
          1,
          tempBuffer.d_pointer(),
          tempBuffer.sizeInBytes,

          outputBuffer.d_pointer(),
          outputBuffer.sizeInBytes,

          &asHandle,

          &emitDesc, 1
      ));
      CUDA_SYNC_CHECK();

      // ==================================================================
      // perform compaction
      // ==================================================================
      uint64_t compactedSize;
      compactedSizeBuffer.download(&compactedSize, 1);

      sceneTlasBuffer.alloc(compactedSize);
      OPTIX_CHECK(optixAccelCompact(optixContext,
          /*stream:*/0,
          asHandle,
          sceneTlasBuffer.d_pointer(),
          sceneTlasBuffer.sizeInBytes,
          &asHandle));
      CUDA_SYNC_CHECK();

      // ==================================================================
      // aaaaaand .... clean up
      // ==================================================================
      outputBuffer.free(); // << the UNcompacted, temporary output buffer
      tempBuffer.free();
      compactedSizeBuffer.free();

      return asHandle;

  }
  /*! helper function that initializes optix and checks for errors */
  void SampleRenderer::initOptix()
  {
    std::cout << "#osc: initializing optix..." << std::endl;
      
    // -------------------------------------------------------
    // check for available optix7 capable devices
    // -------------------------------------------------------
    cudaFree(0);
    int numDevices;
    cudaGetDeviceCount(&numDevices);
    if (numDevices == 0)
      throw std::runtime_error("#osc: no CUDA capable devices found!");
    std::cout << "#osc: found " << numDevices << " CUDA devices" << std::endl;

    // -------------------------------------------------------
    // initialize optix
    // -------------------------------------------------------
    OPTIX_CHECK( optixInit() );
    std::cout << GDT_TERMINAL_GREEN
              << "#osc: successfully initialized optix... yay!"
              << GDT_TERMINAL_DEFAULT << std::endl;
  }

  static void context_log_cb(unsigned int level,
                             const char *tag,
                             const char *message,
                             void *)
  {
    fprintf( stderr, "[%2d][%12s]: %s\n", (int)level, tag, message );
  }

  /*! creates and configures a optix device context (in this simple
    example, only for the primary GPU device) */
  void SampleRenderer::createContext()
  {
    // for this sample, do everything on one device
    const int deviceID = 0;
    CUDA_CHECK(SetDevice(deviceID));
    CUDA_CHECK(StreamCreate(&stream));
      
    cudaGetDeviceProperties(&deviceProps, deviceID);
    std::cout << "#osc: running on device: " << deviceProps.name << std::endl;
      
    CUresult  cuRes = cuCtxGetCurrent(&cudaContext);
    if( cuRes != CUDA_SUCCESS ) 
      fprintf( stderr, "Error querying current context: error code %d\n", cuRes );
      
    OPTIX_CHECK(optixDeviceContextCreate(cudaContext, 0, &optixContext));
    OPTIX_CHECK(optixDeviceContextSetLogCallback
                (optixContext,context_log_cb,nullptr,4));
  }



  /*! creates the module that contains all the programs we are going
    to use. in this simple example, we use a single module from a
    single .cu file, using a single embedded ptx string */
  void SampleRenderer::createModule()
  {
    moduleCompileOptions.maxRegisterCount  = 50;
    moduleCompileOptions.optLevel          = OPTIX_COMPILE_OPTIMIZATION_DEFAULT;
    moduleCompileOptions.debugLevel        = OPTIX_COMPILE_DEBUG_LEVEL_NONE;

    pipelineCompileOptions = {};
    pipelineCompileOptions.traversableGraphFlags = OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_ANY;
    pipelineCompileOptions.usesMotionBlur     = false;
    pipelineCompileOptions.numPayloadValues   = 4;
    pipelineCompileOptions.numAttributeValues = 4;
    pipelineCompileOptions.exceptionFlags     = OPTIX_EXCEPTION_FLAG_NONE;
    pipelineCompileOptions.pipelineLaunchParamsVariableName = "optixLaunchParams";
      
    pipelineLinkOptions.overrideUsesMotionBlur = false;
    pipelineLinkOptions.maxTraceDepth          = 2;
      
    const std::string ptxCode = embedded_ptx_code;
      
    char log[2048];
    size_t sizeof_log = sizeof( log );
    OPTIX_CHECK(optixModuleCreateFromPTX(optixContext,
                                         &moduleCompileOptions,
                                         &pipelineCompileOptions,
                                         ptxCode.c_str(),
                                         ptxCode.size(),
                                         log,&sizeof_log,
                                         &module
                                         ));
    if (sizeof_log > 1) PRINT(log);
  }
    


  /*! does all setup for the raygen program(s) we are going to use */
  void SampleRenderer::createRaygenPrograms()
  {
    // we do a single ray gen program in this example:
    raygenPGs.resize(1);
      
    OptixProgramGroupOptions pgOptions = {};
    OptixProgramGroupDesc pgDesc    = {};
    pgDesc.kind                     = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
    pgDesc.raygen.module            = module;           
    pgDesc.raygen.entryFunctionName = "__raygen__renderFrame";

    // OptixProgramGroup raypg;
    char log[2048];
    size_t sizeof_log = sizeof( log );
    OPTIX_CHECK(optixProgramGroupCreate(optixContext,
                                        &pgDesc,
                                        1,
                                        &pgOptions,
                                        log,&sizeof_log,
                                        &raygenPGs[0]
                                        ));
    if (sizeof_log > 1) PRINT(log);
  }
    
  /*! does all setup for the miss program(s) we are going to use */
  void SampleRenderer::createMissPrograms()
  {
    // we do a single ray gen program in this example:
    missPGs.resize(1);
      
    OptixProgramGroupOptions pgOptions = {};
    OptixProgramGroupDesc pgDesc    = {};
    pgDesc.kind                     = OPTIX_PROGRAM_GROUP_KIND_MISS;
    pgDesc.miss.module            = module;           
    pgDesc.miss.entryFunctionName = "__miss__radiance";

    // OptixProgramGroup raypg;
    char log[2048];
    size_t sizeof_log = sizeof( log );
    OPTIX_CHECK(optixProgramGroupCreate(optixContext,
                                        &pgDesc,
                                        1,
                                        &pgOptions,
                                        log,&sizeof_log,
                                        &missPGs[0]
                                        ));
    if (sizeof_log > 1) PRINT(log);
  }
    
  /*! does all setup for the hitgroup program(s) we are going to use */
  void SampleRenderer::createHitgroupPrograms()
  {
    // for this simple example, we set up a single hit group
    hitgroupPGs.resize(1);

    OptixProgramGroupOptions pgOptions = {};
    OptixProgramGroupDesc pgDesc    = {};
    pgDesc.kind                     = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
    pgDesc.hitgroup.moduleCH            = module;
    pgDesc.hitgroup.entryFunctionNameCH = "__closesthit__radiance";
    pgDesc.hitgroup.moduleAH            = module;
    pgDesc.hitgroup.entryFunctionNameAH = "__anyhit__radiance";
    pgDesc.hitgroup.moduleIS            = module;
    pgDesc.hitgroup.entryFunctionNameIS = "__intersection__is";
     

    char log[2048];
    size_t sizeof_log = sizeof( log );
    OPTIX_CHECK(optixProgramGroupCreate(optixContext,
                                        &pgDesc,
                                        1,
                                        &pgOptions,
                                        log,&sizeof_log,
                                        &hitgroupPGs[0]
                                        ));
    if (sizeof_log > 1) PRINT(log);
  }
    

  /*! assembles the full pipeline of all programs */
  void SampleRenderer::createPipeline()
  {
    std::vector<OptixProgramGroup> programGroups;
    for (auto pg : raygenPGs)
      programGroups.push_back(pg);
    for (auto pg : missPGs)
      programGroups.push_back(pg);
    for (auto pg : hitgroupPGs)
      programGroups.push_back(pg);
      
    char log[2048];
    size_t sizeof_log = sizeof( log );
    OPTIX_CHECK(optixPipelineCreate(optixContext,
                                    &pipelineCompileOptions,
                                    &pipelineLinkOptions,
                                    programGroups.data(),
                                    (int)programGroups.size(),
                                    log,&sizeof_log,
                                    &pipeline
                                    ));
    if (sizeof_log > 1) PRINT(log);

    OPTIX_CHECK(optixPipelineSetStackSize
                (/* [in] The pipeline to configure the stack size for */
                 pipeline, 
                 /* [in] The direct stack size requirement for direct
                    callables invoked from IS or AH. */
                 2*1024,
                 /* [in] The direct stack size requirement for direct
                    callables invoked from RG, MS, or CH.  */                 
                 2*1024,
                 /* [in] The continuation stack requirement. */
                 2*1024,
                 /* [in] The maximum depth of a traversable graph
                    passed to trace. */
                 1));
    if (sizeof_log > 1) PRINT(log);
  }


  /*! constructs the shader binding table */
  void SampleRenderer::buildSBT()
  {
    // ------------------------------------------------------------------
    // build raygen records
    // ------------------------------------------------------------------
    std::vector<RaygenRecord> raygenRecords;
    for (int i=0;i<raygenPGs.size();i++) {
      RaygenRecord rec;
      OPTIX_CHECK(optixSbtRecordPackHeader(raygenPGs[i],&rec));
      rec.data = nullptr; /* for now ... */
      raygenRecords.push_back(rec);
    }
    raygenRecordsBuffer.alloc_and_upload(raygenRecords);
    sbt.raygenRecord = raygenRecordsBuffer.d_pointer();

    // ------------------------------------------------------------------
    // build miss records
    // ------------------------------------------------------------------
    std::vector<MissRecord> missRecords;
    for (int i=0;i<missPGs.size();i++) {
      MissRecord rec;
      OPTIX_CHECK(optixSbtRecordPackHeader(missPGs[i],&rec));
      rec.data = nullptr; /* for now ... */
      missRecords.push_back(rec);
    }
    missRecordsBuffer.alloc_and_upload(missRecords);
    sbt.missRecordBase          = missRecordsBuffer.d_pointer();
    sbt.missRecordStrideInBytes = sizeof(MissRecord);
    sbt.missRecordCount         = (int)missRecords.size();

    // ------------------------------------------------------------------
    // build hitgroup records
    // ------------------------------------------------------------------
    int numMeshes = (int)scene.meshes.size();
    int numSpheres = (int)scene.spheres.size();
    std::vector<HitgroupRecord> hitgroupRecords;
    for (int meshID = 0; meshID < numMeshes; meshID++) {
        HitgroupRecord rec;
        // all meshes use the same code, so all same hit group
        OPTIX_CHECK(optixSbtRecordPackHeader(hitgroupPGs[0], &rec));
        rec.data.triangle_data.color = scene.meshes[meshID].color;
        rec.data.triangle_data.vertex = (vec3f*)vertexBuffer[meshID].d_pointer(); 
        rec.data.triangle_data.index = (vec3i*)indexBuffer[meshID].d_pointer();
        hitgroupRecords.push_back(rec);
    }
    for (int sphereID = 0; sphereID < numSpheres; sphereID++) {
        HitgroupRecord rec;
        OPTIX_CHECK(optixSbtRecordPackHeader(hitgroupPGs[0], &rec));
        rec.data.sphere_data.color = scene.spheres[sphereID].color;
        rec.data.sphere_data.radius = scene.spheres[sphereID].radius;
        hitgroupRecords.push_back(rec);
    }
    hitgroupRecordsBuffer.alloc_and_upload(hitgroupRecords);
    sbt.hitgroupRecordBase          = hitgroupRecordsBuffer.d_pointer();
    sbt.hitgroupRecordStrideInBytes = sizeof(HitgroupRecord);
    sbt.hitgroupRecordCount         = (int)hitgroupRecords.size();
  }



  /*! render one frame */
  void SampleRenderer::render()
  {
    // sanity check: make sure we launch only after first resize is
    // already done:
    if (launchParams.frame.size.x == 0) return;
      
    launchParamsBuffer.upload(&launchParams,1);
      
    OPTIX_CHECK(optixLaunch(/*! pipeline we're launching launch: */
                            pipeline,stream,
                            /*! parameters and SBT */
                            launchParamsBuffer.d_pointer(),
                            launchParamsBuffer.sizeInBytes,
                            &sbt,
                            /*! dimensions of the launch: */
                            launchParams.frame.size.x,
                            launchParams.frame.size.y,
                            1
                            ));
    // sync - make sure the frame is rendered before we download and
    // display (obviously, for a high-performance application you
    // want to use streams and double-buffering, but for this simple
    // example, this will have to do)
    CUDA_SYNC_CHECK();
  }

  /*! set camera to render with */
  void SampleRenderer::setCamera(const Camera &camera)
  {
    lastSetCamera = camera;
    launchParams.camera.position  = camera.from;
    launchParams.camera.direction = normalize(camera.at-camera.from);
    const float cosFovy = 0.66f;
    const float aspect = launchParams.frame.size.x / float(launchParams.frame.size.y);
    launchParams.camera.horizontal
      = cosFovy * aspect * normalize(cross(launchParams.camera.direction,
                                           camera.up));
    launchParams.camera.vertical
      = cosFovy * normalize(cross(launchParams.camera.horizontal,
                                  launchParams.camera.direction));
  }
  
  /*! resize frame buffer to given resolution */
  void SampleRenderer::resize(const vec2i &newSize)
  {
    // resize our cuda frame buffer
    colorBuffer.resize(newSize.x*newSize.y*sizeof(uint32_t));

    // update the launch parameters that we'll pass to the optix
    // launch:
    launchParams.frame.size  = newSize;
    launchParams.frame.colorBuffer = (uint32_t*)colorBuffer.d_pointer();

    // and re-set the camera, since aspect may have changed
    setCamera(lastSetCamera);
  }

  /*! download the rendered color buffer */
  void SampleRenderer::downloadPixels(uint32_t h_pixels[])
  {
    colorBuffer.download(h_pixels,
                         launchParams.frame.size.x*launchParams.frame.size.y);
  }
  
} // ::osc