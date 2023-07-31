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
    let cppCommandPrefix, guessedLang = guessCppCommandAndLang basicInfo in
    let suffix = match guessedLang with
        "c++" -> "ii"
      | "c" -> "i" (* FIXME: other languages are possible *)
      | _ -> failwith (guessedLang ^ " is not a language")
    in
    let (newTempFd, newTempName) = mkstemps ("/tmp/tmp.XXXXXX.cpp." ^ suffix)
        (String.length (".cpp." ^ suffix)) in
    let rewrittenArgs = List.flatten (List.mapi (fun i -> fun argChunk ->
        if i = 0 then [] (* we fill "cpp" or whatever from cppCommandPrefix *) else
        match argChunk with
          | ["-o"; filename] -> ["-o"; newTempName]
          | _ -> argChunk) argChunks)
      @ ( (* we might not have seen "-o" -- ensure there is a -o argument *)
      match basicInfo.minus_o_pos with
        None -> (* there was no -o, so add one *) [ "-o"; newTempName ]
      | _ -> [])
    in
    (* We actually don't want to divert cpp to a temporary file.
     * Can we make that an option? *)
    let infd = openfile newTempName [O_RDONLY] 0o640 in
    (* delete temporary file unless -save-temps *)
    (*let () = if saveTemps then () else unlink newTempName in *)
    (* dup2 our stdout to the original out file, and our stdin to the temp *)
    (* First we copy the prelude to the output. If we don't have a prelude
     * argument (FIXME: look for one in ppPassesToRun) we use the default. *)
    let () = match basicInfo.output_file with
        Some(fname) -> let outFname = (fname ^ ".dbgcov") in
                       let outfd = Unix.openfile outFname [O_RDWR; O_CREAT] 0o640 in
            ((output_string Pervasives.stderr ("output should go to " ^ outFname ^ "\n"); Pervasives.flush Pervasives.stderr);
            dup2 outfd stdout)
          | None -> (
            output_string Pervasives.stderr ("output should go to stdout \n"); Pervasives.flush Pervasives.stderr;
            ()
            )
    in
    let () = dup2 infd stdin in
    let toolPath = if Filename.is_relative Sys.argv.(0) (* TODO: perhaps try Sys.executable_name ? *)
        then Filename.concat Filename.current_dir_name "dbgcov-tool"
        else Filename.concat (Filename.dirname Sys.executable_name) "dbgcov-tool"
    in
    (* FIXME: pass "/dev/stdin" if we can figure out how to tell clang it's C++ *)
    execv toolPath [|toolPath; newTempName ; "--"|]

