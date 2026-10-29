// CPU Particle / Constants 的字节布局必须与本文件一致。
struct Particle { float2 position; float2 velocity; };
cbuffer Parameters : register(b0) {
    uint particleCount;
    float deltaTime;
    float elapsedTime;
    uint gridSize;
};
StructuredBuffer<Particle> particles : register(t0);
RWStructuredBuffer<Particle> writableParticles : register(u0);
struct VertexOutput { float4 position : SV_POSITION; float3 color : COLOR0; };
VertexOutput MakeParticle(float2 p) {
    VertexOutput o;
    o.position=float4(p.x*1.9-0.95,p.y*1.9-0.95,0,1);
    o.color=float3(0.12+p.x*0.75,0.45+p.y*0.5,1.0-p.x*0.35);
    return o;
}
VertexOutput CpuVS(float4 p : POSITION) { return MakeParticle(p.xy); }
VertexOutput GpuVS(uint index : SV_VertexID) { return MakeParticle(particles[index].position); }
float4 PS(VertexOutput input) : SV_TARGET {return float4(input.color,1);}
[numthreads(256,1,1)]
void UpdateCS(uint3 id : SV_DispatchThreadID) {
    // 向上取整派发时，最后一个工作组可能有多余线程。
    if(id.x>=particleCount) return;
    Particle p=writableParticles[id.x];
    p.position+=p.velocity*deltaTime;
    if(p.position.x>=1) p.position.x-=1;
    if(p.position.y>=1) p.position.y-=1;
    if(p.position.x<0) p.position.x+=1;
    if(p.position.y<0) p.position.y+=1;
    writableParticles[id.x]=p;
    // 每个线程独占一个元素；本算法不需要原子或组内 barrier。
}
