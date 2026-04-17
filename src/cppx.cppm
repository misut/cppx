// Umbrella module. `import cppx;` gives access to the small foundation
// surface that most consumers need. Larger capability modules stay
// opt-in so call sites can keep imports and compile times explicit.

export module cppx;

export import cppx.reflect;
export import cppx.platform;
export import cppx.env;
export import cppx.env.system;
