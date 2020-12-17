RWStructuredBuffer<float> inputs : register(u6);

#define num_groups (2)
#define num_threads_x (16)
#define num_threads_total (num_groups * num_threads_x)

[numthreads(num_threads_x, 1, 1)]
void CS(uint3 group_thread_id : SV_GroupThreadID)
{
    int tid = group_thread_id.x;
    int step_size = 1;
    int num_operating_threads = num_threads_total;

    while (num_operating_threads > 0)
    {
        if (tid < num_operating_threads) // still alive?
        {
            int fst = tid * step_size * 2;
            int snd = fst + step_size;

            if (fst < num_threads_total && snd < num_threads_total)
            {
                //inputs[fst] += inputs[snd];
                 inputs[fst] = max(inputs[fst], inputs[snd]);
            }
        }

        step_size <<= 1; // double the step size
        num_operating_threads >>= 1; // halve the amount of operating threads
    }
}
