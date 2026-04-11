// Umbrella module. `import cppx;` gives access to every submodule.
// Prefer importing submodules directly when you only need one
// (`import cppx.reflect;`) for faster compilation.

export module cppx;

export import cppx.reflect;
export import cppx.platform;
export import cppx.env;
export import cppx.env.system;
