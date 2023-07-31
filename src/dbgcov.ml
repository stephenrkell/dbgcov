(* dbgcov.ml -- a simple CIL driver that can be used in place of the C preprocessor,
 *              but generates information about expected debug info coverage.
 *
 * Stephen Kell <stephen.kell@kcl.ac.uk>
 * Copyright 2023 King's College London
 *)
 
(* First we preprocess into a temporary file;
 * we pass through to cpp all our arguments except for any following "-o".
 * Then we run dbgcov and output to the intended -o file.
 *)
open Compiler_args
open Unix

let () =
    let argList = Array.to_list Sys.argv in
    let (argChunks, basicInfo) = scanAndChunkCppArgs argList in
    (* We actually don't want to divert cpp to a temporary file per se,
     * unlike cccppp which really adds a new stage to the pipeline.
     * Instead we want to 'tap' the output of cpp so that as well as its
     * being fed onwards to cc1, we also run it through dbgcov. How can
     * we achieve that? The simplest way is to ensure we output to a
     * file, and run our tool on that file before exiting and allowing
     * compilation to continue. *)
    match basicInfo.output_file with
        None -> failwith "error: cpp command has no output file name (FIXME: support this case)"
      | Some the_input_file_name ->
            (* fork the real cpp and then wait for it *)
            let realArgs = List.flatten (List.mapi (fun i -> fun argChunk ->
                if i = 0 then [] (* we fill "cpp" or whatever from cppCommandPrefix *) else argChunk
            ) argChunks) in
            let cppCommandPrefix, guessedLang = guessCppCommandAndLang basicInfo in
            runCommand "cpp" (cppCommandPrefix @ realArgs);
            (* Run our tool on cpp's output file. We redirect the tool's output
             * to a file named after it. *)
            let toolPath = if Filename.is_relative Sys.argv.(0) (* TODO: perhaps try Sys.executable_name ? *)
                then Filename.concat Filename.current_dir_name "dbgcov-tool"
                else Filename.concat (Filename.dirname Sys.executable_name) "dbgcov-tool"
            in
            let the_output_file_name = the_input_file_name ^ ".dbgcov" in
            let outfd = Unix.openfile the_output_file_name [O_RDWR; O_CREAT] 0o640 in
               ((output_string Pervasives.stderr ("output should go to " ^ the_output_file_name ^ "\n");
                 Pervasives.flush Pervasives.stderr);
                 dup2 outfd stdout);
            execv toolPath [|toolPath; the_input_file_name ; "--"|]
