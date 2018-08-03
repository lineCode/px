/* -----------------------------------------------------------------------------
Copyright (c) 2018 Jose L. Hidalgo (PpluX)

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
----------------------------------------------------------------------------- */

// USAGE
//
// In *ONE* C++ file you need to declare
// #define PX_RENDER_GLTF_IMPLEMENTATION
// before including the file that contains px_render_gltf.h
// 
// px_render_gltf must be included *AFTER* px_render and tiny_gltf.h (when expanding
// the implementation)

#ifndef PX_RENDER_GLTF
#define PX_RENDER_GLTF

#ifndef PX_RENDER
#error px_render must be included before px_render_gltf (because gltf plugin does not include px_render.h)
#endif

namespace px_render {

  struct GLTF {
    struct Flags {
      enum Enum {
        Geometry_Position   = 1<<0, // Vec3f
        Geometry_Normal     = 1<<1, // Vec3f
        Geometry_TexCoord0  = 1<<2, // Vec2f
        // Material_DiffuseTex = 1<<3, (TODO)

        All = 0xFFFFFFFF
      };
    };

    void init(RenderContext *_ctx, const tinygltf::Model &_model, uint32_t import_flags = Flags::All);
    void freeResources();
    ~GLTF() { freeResources(); }

    struct Primitive {
      uint32_t node;
      uint32_t mesh;
      uint32_t index_offset;
      uint32_t index_count;
      // uint32_t texture_diffuse;
    };

    struct Node {
      Mat4 transform; // transformation relative to its parent
      Mat4 model;     // model->world transform
      uint32_t parent = 0;
    };

    RenderContext *ctx = nullptr;
    Buffer vertex_buffer;
    Buffer index_buffer;
    uint32_t num_primitives = 0;
    uint32_t num_nodes = 0;
    std::unique_ptr<Node[]> nodes;
    std::unique_ptr<Primitive[]> primitives;
    std::unique_ptr<Texture[]> textures;
  };
  
}

#endif //PX_RENDER_GLTF


//----------------------------------------------------------------------------
#if defined(PX_RENDER_GLTF_IMPLEMENTATION) && !defined(PX_RENDER_GLTF_IMPLEMENTATION_DONE)

#define PX_RENDER_GTLF_IMPLEMENTATION_DONE
#include <functional>

#ifndef TINY_GLTF_H_
#error tiny_gltf must be included before px_render_gltf (because gltf plugin does not include tiny_gltf.h)
#endif // PX_RENDER_GLTF_IMPLEMENTATION

namespace px_render {

  namespace GLTF_Imp {

    using NodeTraverseFunc = std::function<void(const tinygltf::Model &model, uint32_t raw_node_pos, uint32_t raw_parent_node_pos)>;
    using IndexTraverseFunc = std::function<void(const tinygltf::Model &model, uint32_t index)>;

    static void NodeTraverse(
        const tinygltf::Model &model,
        uint32_t raw_node_pos,
        uint32_t raw_parent_node_pos,
        NodeTraverseFunc func) {
      func(model, raw_node_pos, raw_parent_node_pos);
      const tinygltf::Node &node = model.nodes[raw_node_pos];
      for (uint32_t i = 0; i < node.children.size(); ++i) {
        NodeTraverse(model, node.children[i], raw_node_pos, func);
      }
    }

    static const uint32_t InvalidNode = (uint32_t)-1;

    static void NodeTraverse(const tinygltf::Model &model, NodeTraverseFunc func) {
      int scene_index = std::max(model.defaultScene, 0);
      const tinygltf::Scene &scene = model.scenes[scene_index];
      for (uint32_t i = 0; i < scene.nodes.size(); ++i) {
        NodeTraverse(model, scene.nodes[i], InvalidNode, func);
      }
    }

    static void IndexTraverse(const tinygltf::Model &model, const tinygltf::Accessor &index_accesor, IndexTraverseFunc func) {
      const tinygltf::BufferView &buffer_view = model.bufferViews[index_accesor.bufferView];
      const tinygltf::Buffer &buffer = model.buffers[buffer_view.buffer];
      const uint8_t* base = &buffer.data.at(buffer_view.byteOffset + index_accesor.byteOffset);
      switch (index_accesor.componentType) {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
          const uint32_t *p = (uint32_t*) base;
          for (size_t i = 0; i < index_accesor.count; ++i) {
            func(model, p[i]);
          }
        }; break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
          const uint16_t *p = (uint16_t*) base;
          for (size_t i = 0; i < index_accesor.count; ++i) {
            func(model, p[i]);
          }
        }; break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
          const uint8_t *p = (uint8_t*) base;
          for (size_t i = 0; i < index_accesor.count; ++i) {
            func(model, p[i]);
          }
        }; break;
      }
    }

    static void ExtractVertexData(uint32_t v_pos, const uint8_t *base, int accesor_componentType, int accesor_type, bool accesor_normalized, uint32_t byteStride, float *output, uint8_t max_num_comp) {
      float v[4] = {0.0f, 0.0f, 0.0f, 0.0f};
      uint32_t ncomp = 1;
      switch (accesor_type) {
        case TINYGLTF_TYPE_SCALAR: ncomp = 1; break;
        case TINYGLTF_TYPE_VEC2:   ncomp = 2; break;
        case TINYGLTF_TYPE_VEC3:   ncomp = 3; break;
        case TINYGLTF_TYPE_VEC4:   ncomp = 4; break;
        default:
          assert(!"invalid type");
      }
      switch (accesor_componentType) {
        case TINYGLTF_COMPONENT_TYPE_FLOAT: {
          const float *data = (float*)(base+byteStride*v_pos);
          for (uint32_t i = 0; (i < ncomp); ++i) {
            v[i] = data[i];
          }
        }
        // TODO SUPPORT OTHER FORMATS
        break;
        default:
          assert(!"Conversion Type from float to -> ??? not implemented yet");
          break;
      }
      for (uint32_t i = 0; i < max_num_comp; ++i) {
        output[i] = v[i];
      }
    }
  }

  void GLTF::init(RenderContext *_ctx, const tinygltf::Model &model, uint32_t flags) {
    freeResources();
    ctx = _ctx;
    // nodes 1st pass, count number of nodes+primitives
    uint32_t total_nodes = 1; // always add one artificial root node
    uint32_t total_primitives = 0;
    uint32_t total_num_vertices = 0;
    uint32_t total_num_indices = 0;
    uint32_t const vertex_size = 0
      + (flags&Flags::Geometry_Position?  sizeof(float)*3: 0)
      + (flags&Flags::Geometry_Normal?    sizeof(float)*3: 0)
      + (flags&Flags::Geometry_TexCoord0? sizeof(float)*2: 0)
      ;

    GLTF_Imp::NodeTraverse(model,
      [&total_nodes, &total_primitives, &total_num_vertices, &total_num_indices]
      (const tinygltf::Model model, uint32_t n_pos, uint32_t p_pos) {
      const tinygltf::Node &n = model.nodes[n_pos];
      total_nodes++;
      if (n.mesh >= 0) {
        const tinygltf::Mesh &mesh = model.meshes[n.mesh];
        for(size_t i = 0; i < mesh.primitives.size(); ++i) {
          const tinygltf::Primitive &primitive = mesh.primitives[i];
          if (primitive.indices >= 0) {
            uint32_t min_vertex_index = (uint32_t)-1;
            uint32_t max_vertex_index = 0;
            // TODO: It would be nice to have a cache of index accessors (key=pirmitive.indices)
            // that way if two or more geometries are identical (because they use the same index buffer)
            // then the can reuse the same vertex data. Currently, vertex data is extracted for every
            // primitive....
            GLTF_Imp::IndexTraverse(model, model.accessors[primitive.indices],
              [&min_vertex_index, &max_vertex_index, &total_num_vertices, &total_num_indices]
              (const tinygltf::Model&, uint32_t index) {
                min_vertex_index = std::min(min_vertex_index, index);
                max_vertex_index = std::max(max_vertex_index, index);
                total_num_indices++;
            });
            total_num_vertices += (max_vertex_index - min_vertex_index +1);
            total_primitives++;
          }
        }
      }
    });

    nodes = std::unique_ptr<Node[]>(new Node[total_nodes]);
    primitives = std::unique_ptr<Primitive[]>(new Primitive[total_primitives]);
    std::unique_ptr<float[]> vertex_data {new float[total_num_vertices*vertex_size/sizeof(float)]};
    std::unique_ptr<uint32_t[]> index_data {new uint32_t[total_num_indices]};

    // fill with 0s vertex data
    memset(vertex_data.get(), 0, sizeof(float)*total_num_vertices);

    // nodes 2nd pass, gather info
    nodes[0].model = nodes[0].transform = Mat4::Identity();
    nodes[0].parent = 0; 
    auto node_map = std::unique_ptr<uint32_t[]>(new uint32_t[model.nodes.size()]);
    uint32_t current_node = 1;
    uint32_t current_primitive = 0;
    uint32_t current_mesh = 0;
    uint32_t current_vertex = 0;
    uint32_t current_index = 0;
    GLTF_Imp::NodeTraverse(model,
      [&current_node, &current_primitive, &node_map, &current_mesh,
       &current_vertex, &current_index, &index_data, &vertex_data,
       total_nodes, total_primitives, total_num_vertices, vertex_size, flags, this]
      (const tinygltf::Model &model, uint32_t n_pos, uint32_t p_pos) mutable {
        const tinygltf::Node &gltf_n = model.nodes[n_pos];
        Node &node = nodes[current_node];
        // gather node transform or compute it
        if (gltf_n.matrix.size() == 16) {
          for(size_t i = 0; i < 16; ++i) node.transform.f[i] = gltf_n.matrix[i];
        } else {
          Vec3 s = {1.0f, 1.0f, 1.0f};
          Vec4 r = {0.0f, 0.0f, 0.0f, 0.0f};
          Vec3 t = {0.0f, 0.0f, 0.0f};
          if (gltf_n.scale.size() == 3) {
            s = Vec3{
              (float) gltf_n.scale[0],
              (float) gltf_n.scale[1],
              (float) gltf_n.scale[2]};
          }
          if (gltf_n.rotation.size() == 4) {
            r = Vec4{
              (float) gltf_n.rotation[1], // axis-x
              (float) gltf_n.rotation[2], // axis-y
              (float) gltf_n.rotation[3], // axis-z
              (float) gltf_n.rotation[0]}; // angle
          }
          if (gltf_n.translation.size() == 3) {
            t = Vec3{
              (float) gltf_n.translation[0],
              (float) gltf_n.translation[1],
              (float) gltf_n.translation[2]};
          }
          node.transform = Mat4::SRT(s,r,t);
        }
        // compute node-parent relationship
        node_map[n_pos] = current_node;
        if (p_pos != GLTF_Imp::InvalidNode) {
          node.parent = node_map[p_pos];
          node.model = Mat4::Mult(nodes[node.parent].model, node.transform);
        } else {
          node.parent = 0;
          node.model = node.transform;
        }
        // gather primitive info 
        if (gltf_n.mesh >= 0) {
          const tinygltf::Mesh &mesh = model.meshes[gltf_n.mesh];
          for(size_t i = 0; i < mesh.primitives.size(); ++i) {
            const tinygltf::Primitive &gltf_p = mesh.primitives[i];
            if (gltf_p.indices >= 0) {
              uint32_t min_vertex_index = (uint32_t)-1;
              uint32_t max_vertex_index = 0;
              uint32_t index_count = 0;
              GLTF_Imp::IndexTraverse(model, model.accessors[gltf_p.indices],
                [&min_vertex_index, &max_vertex_index, &current_index, &index_count, &index_data, &current_vertex]
                (const tinygltf::Model&, uint32_t index) {
                  min_vertex_index = std::min(min_vertex_index, index);
                  max_vertex_index = std::max(max_vertex_index, index);
                  index_data[current_index+index_count] = index;
                  index_count++;
              });

              Primitive &primitive = primitives[current_primitive];
              primitive.node = current_node;
              primitive.mesh = current_mesh;
              primitive.index_count = index_count;
              primitive.index_offset = current_index;
              current_index += index_count;

              using AttribWritter = std::function<void(float *w, uint32_t p)> ;

              AttribWritter w_position  = [](float *w, uint32_t p) {};
              AttribWritter w_normal    = [](float *w, uint32_t p) {};
              AttribWritter w_texcoord0 = [](float *w, uint32_t p) {};

              uint32_t vertex_stride_float = vertex_size/sizeof(float);
              for (const auto &attrib : gltf_p.attributes) {

                AttribWritter *writter = nullptr;
                unsigned int max_components = 0;
                if ((flags & Flags::Geometry_Position) && attrib.first.compare("POSITION") == 0) {
                  writter = &w_position;
                  max_components = 3;
                } else if ((flags & Flags::Geometry_Normal) && attrib.first.compare("NORMAL") == 0) {
                  writter = &w_normal;
                  max_components = 3;
                } else if ((flags & Flags::Geometry_TexCoord0) && attrib.first.compare("TEXCOORD_0") == 0) {
                  writter = &w_texcoord0;
                  max_components = 2;
                }

                if (!writter) continue;
                
                const tinygltf::Accessor &accesor = model.accessors[attrib.second];
                const tinygltf::BufferView &buffer_view = model.bufferViews[accesor.bufferView];
                const tinygltf::Buffer &buffer = model.buffers[buffer_view.buffer];
                const uint8_t* base = &buffer.data.at(buffer_view.byteOffset + accesor.byteOffset);
                int byteStride = accesor.ByteStride(buffer_view);
                const bool normalized = accesor.normalized;

                switch (accesor.type) {
                  case TINYGLTF_TYPE_SCALAR: max_components = std::min(max_components, 1u); break;
                  case TINYGLTF_TYPE_VEC2:   max_components = std::min(max_components, 2u); break;
                  case TINYGLTF_TYPE_VEC3:   max_components = std::min(max_components, 3u); break;
                  case TINYGLTF_TYPE_VEC4:   max_components = std::min(max_components, 4u); break;
                }

                switch (accesor.componentType) {
                  case TINYGLTF_COMPONENT_TYPE_FLOAT: *writter =
                    [base, byteStride, max_components](float *w, uint32_t p) {
                      const float *f = (const float *)(base + p*byteStride);
                      for (unsigned int i = 0; i < max_components; ++i) {
                        w[i] = f[i];
                      }
                    }; break;
                  case TINYGLTF_COMPONENT_TYPE_DOUBLE: *writter =
                    [base, byteStride, max_components](float *w, uint32_t p) {
                      const double*f = (const double*)(base + p*byteStride);
                      for (unsigned int i = 0; i < max_components; ++i) {
                        w[i] = f[i];
                      }
                    }; break;
                  case TINYGLTF_COMPONENT_TYPE_BYTE: *writter =
                    [base, byteStride, max_components,normalized](float *w, uint32_t p) {
                      const int8_t *f = (const int8_t*)(base + p*byteStride);
                      for (unsigned int i = 0; i < max_components; ++i) {
                        w[i] = normalized?f[i]/(float)128:f[i];
                      }
                    }; break;
                  case TINYGLTF_COMPONENT_TYPE_SHORT: *writter =
                    [base, byteStride, max_components,normalized](float *w, uint32_t p) {
                      const int16_t *f = (const int16_t*)(base + p*byteStride);
                      for (unsigned int i = 0; i < max_components; ++i) {
                        w[i] = normalized?f[i]/(float)32768:f[i];
                      }
                    }; break;
                  case TINYGLTF_COMPONENT_TYPE_INT: *writter =
                    [base, byteStride, max_components,normalized](float *w, uint32_t p) {
                      const int32_t *f = (const int32_t*)(base + p*byteStride);
                      for (unsigned int i = 0; i < max_components; ++i) {
                        w[i] = normalized?f[i]/(float)2147483648:f[i];
                      }
                    }; break;
                  case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: *writter =
                    [base, byteStride, max_components,normalized](float *w, uint32_t p) {
                      const uint8_t *f = (const uint8_t*)(base + p*byteStride);
                      for (unsigned int i = 0; i < max_components; ++i) {
                        w[i] = normalized?f[i]/(float)255:f[i];
                      }
                    }; break;
                  case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: *writter =
                    [base, byteStride, max_components,normalized](float *w, uint32_t p) {
                      const uint16_t *f = (const uint16_t*)(base + p*byteStride);
                      for (unsigned int i = 0; i < max_components; ++i) {
                        w[i] = normalized?f[i]/(float)65535:f[i];
                      }
                    }; break;
                  case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: *writter =
                    [base, byteStride, max_components,normalized](float *w, uint32_t p) {
                      const uint32_t *f = (const uint32_t*)(base + p*byteStride);
                      for (unsigned int i = 0; i < max_components; ++i) {
                        w[i] = normalized?f[i]/(float)4294967295:f[i];
                      }
                    }; break;
                  default:
                    assert(!"Not supported component type (yet)");
                }
              }

              uint32_t vertex_offset = 0;

              if (flags & Flags::Geometry_Position) {
                for (uint32_t i = 0 ; i <= max_vertex_index-min_vertex_index; ++i) {
                  w_position(&vertex_data[vertex_offset+(current_vertex+i)*vertex_stride_float], i+min_vertex_index);
                }
                vertex_offset += 3;
              }
              if (flags & Flags::Geometry_Normal) {
                for (uint32_t i = 0; i <= max_vertex_index-min_vertex_index; ++i) {
                  w_normal(&vertex_data[vertex_offset+(current_vertex+i)*vertex_stride_float], i+min_vertex_index);
                }
                vertex_offset += 3;
              }
              if (flags & Flags::Geometry_TexCoord0) {
                for (uint32_t i = 0; i <= max_vertex_index-min_vertex_index; ++i) {
                  w_texcoord0(&vertex_data[vertex_offset+(current_vertex+i)*vertex_stride_float], i+min_vertex_index);
                }
                vertex_offset += 2;
              }

              for (uint32_t i = 0; i < primitive.index_count; ++i) {
                index_data[primitive.index_offset+i] += current_vertex;
                index_data[primitive.index_offset+i] -= min_vertex_index;
              }
              
              current_vertex += max_vertex_index - min_vertex_index+1;
              current_primitive++;
            }
          }
          current_mesh++;
        }
        current_node++;
    });

    DisplayList dl;
    vertex_buffer = ctx->createBuffer({BufferType::Vertex, vertex_size*total_num_vertices, Usage::Static});
    index_buffer  = ctx->createBuffer({BufferType::Index, sizeof(uint32_t)*total_num_indices, Usage::Static});
    dl.fillBufferCommand()
      .set_buffer(vertex_buffer)
      .set_data(vertex_data.get())
      .set_size(vertex_size*total_num_vertices);
    dl.fillBufferCommand()
      .set_buffer(index_buffer)
      .set_data(index_data.get())
      .set_size(sizeof(uint32_t)*total_num_indices);
    ctx->submitDisplayList(std::move(dl));

    num_nodes = total_nodes;
    num_primitives = total_primitives;
  }

  void GLTF::freeResources() {
    if (num_nodes) {
      num_nodes = 0;
      num_primitives = 0;
      nodes.reset();
      primitives.reset();
      DisplayList dl;
      dl.destroy(vertex_buffer);
      dl.destroy(index_buffer);
      ctx->submitDisplayList(std::move(dl));
    }
  }
}

#endif // PX_RENDER_GLTF_IMPLEMENTATION