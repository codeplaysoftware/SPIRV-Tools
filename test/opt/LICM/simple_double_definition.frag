#version 440 core
layout(location = 0) out vec4 c;
layout(location = 1) out vec4 d;
layout(location = 2) in vec4 in_val;
void main(){
  int a = 1;
  int b = 2;
  int hoist = 0;
  c = vec4(0,0,0,0);
  for (int i = int(in_val.x); i < int(in_val.y); i++) {
    // hoist is not invariant, due to double definition
    hoist = a + b;
    c = vec4(i,i,i,i);
    hoist = 0;
    d = vec4(hoist, i, in_val.z, in_val.w);
  }
}
