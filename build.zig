const std = @import("std");
const builtin = @import("builtin");

pub const Options = struct {
    pub const max_task_priorities = 5;

    shared: bool,
    build_examples: bool,
    task_priorities: u32,

    const defaults = Options{
        .shared = false,
        .build_examples = false,
        .task_priorities = 0,
    };

    pub fn getOptions(b: *std.Build) !Options {
        const task_priorities = b.option(u32, "task_priorities", "Number of task priorities, 1-5, 0 for defined by defaults in source") orelse defaults.task_priorities;
        if (task_priorities > max_task_priorities) {
            std.log.err("task_priorities must be in range 0-{d}", .{max_task_priorities});
            return error.InvalidTaskPriorities;
        }

        return .{
            .shared = b.option(bool, "shared", "Compile as shared library") orelse defaults.shared,
            .build_examples = b.option(bool, "build_examples", "Build examples") orelse defaults.build_examples,
            .task_priorities = task_priorities,
        };
    }

    pub fn applyTaskPriorities(options: Options, b: *std.Build, compile: *std.Build.Step.Compile) !void {
        if (options.task_priorities > 0) {
            const task_priorities_str = try std.fmt.allocPrint(b.allocator, "{d}", .{options.task_priorities});
            // leak task_priorities_str as we dont really care in build scripts and lifetime needs to be longer ...
            compile.root_module.addCMacro("ENKITS_TASK_PRIORITIES_NUM", task_priorities_str);
        }
    }
};

pub fn build(b: *std.Build) !void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    const options = try Options.getOptions(b);
    const lib = try compileEnkiTS(b, target, optimize, options);

    b.installArtifact(lib);

    if (options.build_examples) {
        try buildExamples(b, target, optimize, lib, options);
    }
}

fn compileEnkiTS(b: *std.Build, target: std.Build.ResolvedTarget, optimize: std.builtin.OptimizeMode, options: Options) !*std.Build.Step.Compile {
    const module = b.addModule("enki_ts", .{
        .target = target,
        .optimize = optimize,
        .link_libcpp = true,
    });

    const linkage: std.builtin.LinkMode = if (options.shared) .dynamic else .static;
    const enki_ts = b.addLibrary(.{
        .root_module = module,
        .name = "enki_ts",
        .linkage = linkage,
    });

    const cpp_source_files = &[_][]const u8{
        "src/LockLessMultiReadPipe.h",
        "src/TaskScheduler_c.h",
        "src/TaskScheduler_c.cpp",
        "src/TaskScheduler.cpp",
        "src/TaskScheduler.h",
    };

    enki_ts.installHeader(b.path("src/TaskScheduler_c.h"), "TaskScheduler_c.h");
    enki_ts.installHeader(b.path("src/TaskScheduler.h"), "TaskScheduler.h");

    var flags_arr = std.ArrayList([]const u8).empty;
    defer flags_arr.deinit(b.allocator);

    const target_os = target.query.os_tag orelse builtin.target.os.tag;
    if (target_os == .macos) {
        try flags_arr.append(b.allocator, "-stdlib=libc++");
    } else {
        try flags_arr.append(b.allocator, "-std=c++11");
    }

    if (target_os == .linux) {
        try flags_arr.append(b.allocator, "-pthread");
    }

    if (options.shared) {
        try flags_arr.appendSlice(b.allocator, &[_][]const u8{
            "-fPIC",
            "-DBUILD_LIBTYPE_SHARED",
        });

        enki_ts.root_module.addCMacro("ENKITS_BUILD_DLL", "1");
        enki_ts.root_module.addCMacro("ENKITS_DLL", "1");
    }

    try options.applyTaskPriorities(b, enki_ts);

    enki_ts.root_module.addCSourceFiles(.{
        .files = cpp_source_files,
        .flags = flags_arr.items,
        .language = .cpp,
    });

    return enki_ts;
}

fn buildExamples(
    b: *std.Build,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
    enki_ts: *std.Build.Step.Compile,
    options: Options,
) !void {
    inline for (&[_][]const u8{
        "CompletionAction",
        "CustomAllocator",
        "Dependencies",
        "ExternalTaskThread",
        "LambdaTask",
        "ParallelSum",
        "PinnedTask",
        "TaskOverhead",
        "TaskThroughput",
        "TestAll",
        "TestWaitforTask",
        "WaitForNewPinnedTasks",
    }) |cpp_example_name| {
        try buildCppExample(b, target, optimize, enki_ts, options, cpp_example_name);
    }

    inline for (&[_][]const u8{
        "CompletionAction_c",
        "CustomAllocator_c",
        "Dependencies_c",
        "ExternalTaskThread_c",
        "ParallelSum_c",
        "PinnedTask_c",
        "WaitForNewPinnedTasks_c",
    }) |c_example_name| {
        try buildCExample(b, target, optimize, enki_ts, options, c_example_name);
    }

    if (options.task_priorities > 0) {
        try buildCppExample(b, target, optimize, enki_ts, options, "Priorities");
        try buildCExample(b, target, optimize, enki_ts, options, "Priorities_c");
    }
}

fn buildCppExample(
    b: *std.Build,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
    enki_ts: *std.Build.Step.Compile,
    options: Options,
    comptime example_name: []const u8,
) !void {
    const exe_mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
    });

    const exe = b.addExecutable(.{
        .name = example_name,
        .root_module = exe_mod,
    });

    const example_path = "example/" ++ example_name ++ ".cpp";
    const source_files = [_][]const u8{example_path};

    exe.root_module.addCSourceFiles(.{
        .files = &source_files,
        .flags = &[_][]const u8{
            "-std=c++11",
        },
        .language = .cpp,
    });

    try options.applyTaskPriorities(b, exe);

    // link with lib
    exe.root_module.linkLibrary(enki_ts);

    b.installArtifact(exe);

    addExampleRunStep(b, exe, example_name);
}

fn buildCExample(
    b: *std.Build,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
    enki_ts: *std.Build.Step.Compile,
    options: Options,
    comptime example_name: []const u8,
) !void {
    const exe_mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
    });

    const exe = b.addExecutable(.{
        .name = example_name,
        .root_module = exe_mod,
    });

    const example_path = "example/" ++ example_name ++ ".c";
    const source_files = [_][]const u8{example_path};

    exe.root_module.addCSourceFiles(.{
        .files = &source_files,
        .flags = &[_][]const u8{},
        .language = .c,
    });

    try options.applyTaskPriorities(b, exe);

    // link with lib
    exe.root_module.linkLibrary(enki_ts);

    b.installArtifact(exe);

    addExampleRunStep(b, exe, example_name);
}

fn addExampleRunStep(
    b: *std.Build,
    exe: *std.Build.Step.Compile,
    comptime example_name: []const u8,
) void {
    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());
    const run_step = b.step("run" ++ "-" ++ example_name, "Run the " ++ example_name ++ " example");
    run_step.dependOn(&run_cmd.step);
}
