struct pass_data
{
    matrix view;
    matrix proj;
    float3 eye_pos;
    float time;
    float delta_time;
    float aspect_ratio;
    float vert_cotangent;
};
ConstantBuffer<pass_data> cb_pass : register(b0);

struct model
{
    matrix model;
};
ConstantBuffer<model> cb_object : register(b3);

// Random number generator
static uint rng_state;

void rand_seed(int seed)
{
    // wang hash from: http://www.burtleburtle.net/bob/hash/integer.html
    seed = (seed ^ 61) ^ (seed >> 16);
    seed *= 9;
    seed = seed ^ (seed >> 4);
    seed *= 0x27d4eb2d;
    seed = seed ^ (seed >> 15);

    // set starting seed
    rng_state = seed;
}

float rand()
{
    // LCG values from Numerical Recipes
    rng_state = 1664525 * rng_state + 1013904223;

    // Generate a random float in [0, 1)...
    return float(rng_state) * (1.0 / 4294967296.0);
}

struct bounding_box
{
    float3 position[8];
};

// We use an append buffer because many different dispatch calls will append to it
AppendStructuredBuffer<bounding_box> bounds : register(u3);
