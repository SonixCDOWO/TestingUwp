// Triángulo a pantalla completa sin buffers: solo SV_VertexID
float4 VSMain(uint id : SV_VertexID) : SV_Position {
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p * 2.0 - 1.0, 0.0, 1.0);
}
float4 PSMain() : SV_Target {
    return float4(1.0, 0.0, 0.0, 1.0);
}
