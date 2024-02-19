; RUN: %symcc -O2 %s -o %t
; RUN: echo -ne "\x05\x00\x00\x00" | %t 2>&1 | %filecheck %s

%struct._IO_FILE = type { i32, i8*, i8*, i8*, i8*, i8*, i8*, i8*, i8*, i8*, i8*, i8*, %struct._IO_marker*, %struct._IO_FILE*, i32, i32, i64, i16, i8, [1 x i8], i8*, i64, i8*, i8*, i8*, i8*, i64, i32, [20 x i8] }
%struct._IO_marker = type { %struct._IO_marker*, %struct._IO_FILE*, i32 }

@g_value = dso_local local_unnamed_addr global i16 40, align 2
@stderr = external dso_local local_unnamed_addr global %struct._IO_FILE*, align 8
@.str = private unnamed_addr constant [18 x i8] c"Failed to read x\0A\00", align 1
@.str.1 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@.str.2 = private unnamed_addr constant [4 x i8] c"yes\00", align 1
@.str.3 = private unnamed_addr constant [3 x i8] c"no\00", align 1

; Function Attrs: nofree nounwind uwtable
define dso_local i32 @main(i32 %argc, i8** nocapture readnone %argv) local_unnamed_addr #0 {
entry:
  %x = alloca i16, align 2
  %0 = bitcast i16* %x to i8*
  %call = call i64 @read(i32 0, i8* nonnull %0, i64 2) #5
  %cmp.not = icmp eq i64 %call, 2
  %1 = load %struct._IO_FILE*, %struct._IO_FILE** @stderr, align 8
  br i1 %cmp.not, label %if.end, label %if.then

if.then:                                          ; preds = %entry
  %2 = call i64 @fwrite(i8* getelementptr inbounds ([18 x i8], [18 x i8]* @.str, i64 0, i64 0), i64 17, i64 1, %struct._IO_FILE* %1) #6
  br label %cleanup

if.end:                                           ; preds = %entry
  %3 = load i16, i16* %x, align 2
  %4 = load i16, i16* @g_value, align 2
  %add = call i16 @llvm.uadd.sat.i16(i16 %3, i16 %4)
  %cmp = icmp eq i16 %add, 43981
  %cond = select i1 %cmp, i8* getelementptr inbounds ([4 x i8], [4 x i8]* @.str.2, i64 0, i64 0), i8* getelementptr inbounds ([3 x i8], [3 x i8]* @.str.3, i64 0, i64 0)
  ; SIMPLE: Trying to solve
  ; SIMPLE: Found diverging input
  ; SIMPLE-DAG: stdin0 -> #xa5
  ; SIMPLE-DAG: stdin1 -> #xab
  ; ANY: no
  %call5 = call i32 (%struct._IO_FILE*, i8*, ...) @fprintf(%struct._IO_FILE* %1, i8* getelementptr inbounds ([4 x i8], [4 x i8]* @.str.1, i64 0, i64 0), i8* %cond) #6
  br label %cleanup

cleanup:                                          ; preds = %if.end, %if.then
  %retval.0 = phi i32 [ -1, %if.then ], [ 0, %if.end ]
  ret i32 %retval.0
}

declare i64 @read(i32, i8* nocapture, i64)
declare i32 @fprintf(%struct._IO_FILE* nocapture , i8* nocapture readonly, ...)
declare i64 @fwrite(i8* nocapture, i64, i64, %struct._IO_FILE* nocapture)
declare i16 @llvm.uadd.sat.i16(i16, i16)
