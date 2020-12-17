#include "common.hlsl"

struct physics
{
    float drag_coeff_k1;
    float drag_coeff_k2;
};
ConstantBuffer<physics> cb_physics : register(b4);

struct per_particle_system_constants
{
    float dt_accum;
    int missed_frames;
};
ConstantBuffer<per_particle_system_constants> cb_ps_data : register(b5);

// input layout
struct vertex_in
{
    float3 position : POSITION;
    float size : SIZE;
    float3 velocity : VELOCITY;
    float age : AGE;
};

struct particle
{
    float3 position;
    float size;
    float3 velocity;
    float age;
};
RWStructuredBuffer<particle> output_particle : register(u0);
RWStructuredBuffer<particle> input_particle : register(u1);

#define threads_count (512)
[numthreads(threads_count, 1, 1)]
void CS(uint3 thread_id : SV_DispatchThreadID)
{
    int index = thread_id.x;

    // Initialize random number generator
    rand_seed(index);

    particle p = input_particle[index];

    float damping = 0.90f;
    float3 gravity = float3(0.f, -9.8f, 0.f);
    float dt = cb_pass.delta_time;
    float3 last_velocity = float3(0.f, 0.f, 0.f);

    if (cb_ps_data.dt_accum > 0.f)
    {
        //dt += cb_ps_data.dt_accum;

        // Fast forward velocity n frames
        int missed_frames = (int) floor(cb_ps_data.dt_accum / (1.f / 144.f));
        while (missed_frames > 0)
        {
            float3 acceleration = gravity;
            last_velocity = p.velocity;
    // Drag
            float3 force = p.velocity;

            float k1 = cb_physics.drag_coeff_k1;
            float k2 = cb_physics.drag_coeff_k2;

            float drag_coefficient = length(force);
            drag_coefficient = k1 * drag_coefficient
                     + k2 * drag_coefficient * drag_coefficient; // Drag is stronger at higher speeds

            float3 drag_force = normalize(force);
            drag_force = drag_force * -drag_coefficient; // Counter the existing force to go in the opposite direction

    // Acceleration from forces
            acceleration += drag_force;

            p.velocity += acceleration * dt;
            p.position += ((last_velocity + p.velocity) * 0.5f) * dt;
            missed_frames -= 1;
        }
    }
    else
    {
        float3 acceleration = gravity;
        last_velocity = p.velocity;
    // Drag
        float3 force = p.velocity;

        float k1 = cb_physics.drag_coeff_k1;
        float k2 = cb_physics.drag_coeff_k2;

        float drag_coefficient = length(force);
        drag_coefficient = k1 * drag_coefficient
                     + k2 * drag_coefficient * drag_coefficient; // Drag is stronger at higher speeds

        float3 drag_force = normalize(force);
        drag_force = drag_force * -drag_coefficient; // Counter the existing force to go in the opposite direction

    // Acceleration from forces
        acceleration += drag_force;
        // Velocity
        p.velocity += acceleration * dt;
        p.position += ((last_velocity + p.velocity) * 0.5f) * dt;
    }
    // Velocity
    //p.velocity += acceleration * dt;

    // Damping
    //p.velocity *= pow(damping, dt);

    // Apply velocity verlet integration by averaging the new velocity with the last velocity
    // This helps keeping the simulation more stable over time
    //p.position += ((last_velocity + p.velocity) * 0.5f) * dt;

    // Apply velocity
    //p.position += p.velocity * dt;

    // Output the particle with the simulation integrated to it
    output_particle[index] = p;
}