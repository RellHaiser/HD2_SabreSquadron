# Particle effects (source files 0x0043f2a0-0x00440420 particle, 0x00440440-0x00443c90 emitter,
# 0x00443ce0-0x00447f60 C_ParticlesModule).
#
# tables\effects.def: effects (chunk 0x3F2) made of particle definitions (0x67), each with
# texture animation frames and motion phases (0xCB). missions\<m>\effects.bin: static emitters.

# One texture animation frame: UV rectangle.
struct S_ParticleFrame size=0x10
  0x00 float u0
  0x04 float v0
  0x08 float u1
  0x0c float v1
end

# Common base of the definitions (vtable 0x80f9e8): type 1 effect, 2 particle, 3 phase.
class S_EffectItem size=0x8 vtable=0x80f9e8:1
  0x04 int m_type
end

# Motion phase of a particle (chunk 0xCB / 300).
class S_ParticlePhase : S_EffectItem size=0x90 vtable=0x80f9ec:1
  0x08 C_str m_name
  0x10 S_vector m_dir
  0x1c float m_dirW
  0x20 S_vector m_dirVariance
  0x30 int m_duration
  0x34 int m_durationVariance
  0x38 float m_gravity
  0x3c float m_rotSpeed
  0x40 float m_rotSpeedVariance
  0x44 float m_endSize
  0x48 float m_endSizeVariance
  0x50 S_vector m_color
  0x5c float m_colorW
  0x60 S_vector m_colorVariance
  0x70 float m_unknown70
  0x74 float m_unknown74
  0x78 float m_endAlpha
  0x7c float m_endAlphaVariance
  0x80 float m_accel
  0x84 float m_accelVariance
end

# One particle kind of an effect (chunk 0x67).
class S_ParticleDef : S_EffectItem size=0xa0 vtable=0x80f9e4:1
  0x08 C_str m_name
  0x0c std::vector<S_ParticleFrame> m_frames
  0x1c int m_loopFrame
  0x20 int m_frameTime
  0x24 int m_burstMin
  0x28 int m_burstMax
  0x2c int m_rate
  0x30 int m_unknown30
  0x34 int m_fadeStart
  0x38 int m_fadeRate
  0x3c int m_repeat
  0x40 int m_delay
  0x44 uint32_t m_flags
  0x50 S_vector m_pos
  0x60 S_vector m_posVariance
  0x70 float m_size
  0x74 float m_sizeVariance
  0x78 float m_speed
  0x7c float m_speedVariance
  0x80 float m_alpha
  0x84 float m_alphaVariance
  0x88 float m_rotation
  0x8c float m_rotationVariance
  0x90 std::vector<S_ParticlePhase*> m_phases
end

# An effect of tables\effects.def (chunk 0x3F2).
class S_EffectDef : S_EffectItem size=0x38 vtable=0x80f9e0:1
  0x08 C_str m_name
  0x0c C_str m_texture
  0x10 bool m_lit
  0x14 int m_blendMode
  0x18 float m_range
  0x1c float m_unknown1C
  0x20 int m_srcBlend
  0x24 int m_dstBlend
  0x28 std::vector<S_ParticleDef*> m_particles
end

# One live particle (16-byte aligned, from the particle pool).
struct S_Particle size=0xb0
  0x00 S_matrix m_matrix
  0x40 int m_frame
  0x44 int m_numFrames
  0x48 int m_frameTimer
  0x4c float m_size
  0x50 float m_sizeSpeed
  0x54 float m_rotation
  0x58 float m_rotSpeed
  0x5c int m_lifetime
  0x60 float m_alpha
  0x64 float m_alphaSpeed
  0x68 float m_speed
  0x6c float m_accel
  0x70 float m_gravity
  0x80 S_vector m_color
  0x8c float m_colorW
  0x90 S_ParticleDef* m_def
  0x94 int m_phase
  0x98 S_ParticleFrame m_uv
  0xa8 uint32_t m_flags
  0xac int m_defIndex
end

struct S_ParticlePoolNode size=0x8
  0x00 S_Particle* particle
  0x04 S_ParticlePoolNode* next
end

# Free list of particles (g_particlePool, vtable 0x80f990).
class C_ParticlePool size=0x14 vtable=0x80f990:1
  0x04 S_ParticlePoolNode* m_free
  0x08 S_ParticlePoolNode* m_spareNodes
  0x0c int m_growBy
  0x10 int m_count
end

# Emission state of one particle kind of an emitter (saved as chunk 0x2AFA).
struct S_EmitterState size=0x18
  0x00 int m_delay
  0x04 int m_timer
  0x08 int m_age
  0x0c int m_interval
  0x10 float m_rate
  0x14 int m_repeat
end

# A running effect: its particles drawn by one "MR_Particle" visual.
struct C_ParticleEmitter size=0xe0
  0x00 I3D_scene* m_scene
  0x04 I3D_particle_visual* m_visual
  0x10 S_matrix m_matrix
  0x50 S_matrix m_localMatrix
  0x90 I3D_frame* m_parent
  0x94 int m_unknown94
  0x98 float m_rangeSq
  0x9c uint32_t m_flags
  0xa0 float m_brightness
  0xa4 bool m_empty
  0xa8 float m_depth
  0xac S_EffectDef* m_def
  0xb0 std::vector<S_Particle*> m_particles
  0xc0 std::vector<S_EmitterState> m_states
  0xd0 bool m_faceCamera
end

struct S_ActiveEmitter size=0x10
  0x00 C_ParticleEmitter* emitter
  0x04 int handle
  0x08 int effect
  0x0c bool transient
end

class C_ParticlesModule : C_Module size=0x1048 vtable=0x80f9a0:16
  0x000c std::vector<C_ParticleEmitter*> m_freeEmitters[256]
  0x100c I3D_scene* m_scene
  0x1010 std::vector<S_ActiveEmitter> m_active
  0x1020 std::vector<S_EffectDef*> m_effects
  0x1030 std::vector<S_ActiveEmitter> m_static
  0x1040 int m_lastHandle
  0x1044 bool m_started
end
