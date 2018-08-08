vulkan_shaders_src = [
	'texture.vert',
	'texture.frag',
]

vulkan_shaders = []
foreach shader : vulkan_shaders_src
	name = shader.underscorify() + '_data'
	args = [glslang, '-V', '@INPUT@', '-o', '@OUTPUT@', '--vn', name]
	header = custom_target(
		shader + '_spv',
		output: shader + '.h',
		input: shader,
		command: args)

	vulkan_shaders += [header]
endforeach
