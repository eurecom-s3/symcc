; RUN: %symcc -O3 -c %s -o %t
;
; This file exposed a bug in our handling of "invoke" instructions that would
; lead to invalid byte code.

; ModuleID = '/home/seba/work/compiler/llvm-project/libcxxabi/src/cxa_vector.cpp'
source_filename = "/home/seba/work/compiler/llvm-project/libcxxabi/src/cxa_vector.cpp"
target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

$__clang_call_terminate = comdat any

; Function Attrs: sspstrong uwtable
define nonnull i8* @__cxa_vec_new(i64 %arg, i64 %arg1, i64 %arg2, void (i8*)* %arg3, void (i8*)* %arg4) local_unnamed_addr #0 personality i32 (...)* @__gxx_personality_v0 {
bb:
  %tmp = mul i64 %arg1, %arg
  %tmp5 = add i64 %tmp, %arg2
  %tmp6 = tail call i8* @_Znam(i64 %tmp5)
  %tmp7 = icmp eq i64 %arg2, 0
  br i1 %tmp7, label %bb13, label %bb8

bb8:                                              ; preds = %bb
  %tmp9 = getelementptr inbounds i8, i8* %tmp6, i64 %arg2
  %tmp10 = getelementptr inbounds i8, i8* %tmp9, i64 -8
  %tmp11 = bitcast i8* %tmp10 to i64*
  store i64 %arg, i64* %tmp11, align 8, !tbaa !3
  br label %bb13

bb12:                                             ; preds = %bb32, %bb25
  tail call void @_ZdaPv(i8* nonnull %tmp6)
  resume { i8*, i32 } %tmp26

bb13:                                             ; preds = %bb8, %bb
  %tmp14 = phi i8* [ %tmp9, %bb8 ], [ %tmp6, %bb ]
  %tmp15 = icmp ne void (i8*)* %arg3, null
  %tmp16 = icmp ne i64 %arg, 0
  %tmp17 = and i1 %tmp16, %tmp15
  br i1 %tmp17, label %bb18, label %bb41

bb18:                                             ; preds = %bb21, %bb13
  %tmp19 = phi i8* [ %tmp23, %bb21 ], [ %tmp14, %bb13 ]
  %tmp20 = phi i64 [ %tmp22, %bb21 ], [ 0, %bb13 ]
  invoke void %arg3(i8* %tmp19)
          to label %bb21 unwind label %bb25

bb21:                                             ; preds = %bb18
  %tmp22 = add nuw i64 %tmp20, 1
  %tmp23 = getelementptr inbounds i8, i8* %tmp19, i64 %arg1
  %tmp24 = icmp eq i64 %tmp22, %arg
  br i1 %tmp24, label %bb41, label %bb18

bb25:                                             ; preds = %bb18
  %tmp26 = landingpad { i8*, i32 }
          cleanup
  %tmp27 = icmp eq void (i8*)* %arg4, null
  br i1 %tmp27, label %bb12, label %bb28

bb28:                                             ; preds = %bb25
  %tmp29 = mul i64 %tmp20, %arg1
  %tmp30 = getelementptr inbounds i8, i8* %tmp14, i64 %tmp29
  %tmp31 = sub i64 0, %arg1
  br label %bb32

bb32:                                             ; preds = %bb36, %bb28
  %tmp33 = phi i64 [ %tmp20, %bb28 ], [ %tmp37, %bb36 ]
  %tmp34 = phi i8* [ %tmp30, %bb28 ], [ %tmp38, %bb36 ]
  %tmp35 = icmp eq i64 %tmp33, 0
  br i1 %tmp35, label %bb12, label %bb36

bb36:                                             ; preds = %bb32
  %tmp37 = add i64 %tmp33, -1
  %tmp38 = getelementptr inbounds i8, i8* %tmp34, i64 %tmp31
  invoke void %arg4(i8* %tmp38)
          to label %bb32 unwind label %bb39

bb39:                                             ; preds = %bb36
  %tmp40 = landingpad { i8*, i32 }
          catch i8* null
  tail call void @_ZSt9terminatev() #5
  unreachable

bb41:                                             ; preds = %bb21, %bb13
  ret i8* %tmp14
}

; Function Attrs: sspstrong uwtable
define i8* @__cxa_vec_new2(i64 %arg, i64 %arg1, i64 %arg2, void (i8*)* %arg3, void (i8*)* %arg4, i8* (i64)* nocapture %arg5, void (i8*)* %arg6) local_unnamed_addr #0 personality i8* bitcast (i32 (...)* @__gxx_personality_v0 to i8*) {
bb:
  %tmp = mul i64 %arg1, %arg
  %tmp7 = add i64 %tmp, %arg2
  %tmp8 = tail call i8* %arg5(i64 %tmp7)
  %tmp9 = icmp eq i8* %tmp8, null
  br i1 %tmp9, label %bb49, label %bb10

bb10:                                             ; preds = %bb
  %tmp11 = icmp eq i64 %arg2, 0
  br i1 %tmp11, label %bb21, label %bb12

bb12:                                             ; preds = %bb10
  %tmp13 = getelementptr inbounds i8, i8* %tmp8, i64 %arg2
  %tmp14 = getelementptr inbounds i8, i8* %tmp13, i64 -8
  %tmp15 = bitcast i8* %tmp14 to i64*
  store i64 %arg, i64* %tmp15, align 8, !tbaa !3
  br label %bb21

bb16:                                             ; preds = %bb40, %bb33
  invoke void %arg6(i8* nonnull %tmp8)
          to label %bb20 unwind label %bb17

bb17:                                             ; preds = %bb16
  %tmp18 = landingpad { i8*, i32 }
          catch i8* null
  %tmp19 = extractvalue { i8*, i32 } %tmp18, 0
  tail call void @__clang_call_terminate(i8* %tmp19) #5
  unreachable

bb20:                                             ; preds = %bb16
  resume { i8*, i32 } %tmp34

bb21:                                             ; preds = %bb12, %bb10
  %tmp22 = phi i8* [ %tmp13, %bb12 ], [ %tmp8, %bb10 ]
  %tmp23 = icmp ne void (i8*)* %arg3, null
  %tmp24 = icmp ne i64 %arg, 0
  %tmp25 = and i1 %tmp24, %tmp23
  br i1 %tmp25, label %bb26, label %bb49

bb26:                                             ; preds = %bb29, %bb21
  %tmp27 = phi i8* [ %tmp31, %bb29 ], [ %tmp22, %bb21 ]
  %tmp28 = phi i64 [ %tmp30, %bb29 ], [ 0, %bb21 ]
  invoke void %arg3(i8* %tmp27)
          to label %bb29 unwind label %bb33

bb29:                                             ; preds = %bb26
  %tmp30 = add nuw i64 %tmp28, 1
  %tmp31 = getelementptr inbounds i8, i8* %tmp27, i64 %arg1
  %tmp32 = icmp eq i64 %tmp30, %arg
  br i1 %tmp32, label %bb49, label %bb26

bb33:                                             ; preds = %bb26
  %tmp34 = landingpad { i8*, i32 }
          cleanup
  %tmp35 = icmp eq void (i8*)* %arg4, null
  br i1 %tmp35, label %bb16, label %bb36

bb36:                                             ; preds = %bb33
  %tmp37 = mul i64 %tmp28, %arg1
  %tmp38 = getelementptr inbounds i8, i8* %tmp22, i64 %tmp37
  %tmp39 = sub i64 0, %arg1
  br label %bb40

bb40:                                             ; preds = %bb44, %bb36
  %tmp41 = phi i64 [ %tmp28, %bb36 ], [ %tmp45, %bb44 ]
  %tmp42 = phi i8* [ %tmp38, %bb36 ], [ %tmp46, %bb44 ]
  %tmp43 = icmp eq i64 %tmp41, 0
  br i1 %tmp43, label %bb16, label %bb44

bb44:                                             ; preds = %bb40
  %tmp45 = add i64 %tmp41, -1
  %tmp46 = getelementptr inbounds i8, i8* %tmp42, i64 %tmp39
  invoke void %arg4(i8* %tmp46)
          to label %bb40 unwind label %bb47

bb47:                                             ; preds = %bb44
  %tmp48 = landingpad { i8*, i32 }
          catch i8* null
  tail call void @_ZSt9terminatev() #5
  unreachable

bb49:                                             ; preds = %bb29, %bb21, %bb
  %tmp50 = phi i8* [ null, %bb ], [ %tmp22, %bb21 ], [ %tmp22, %bb29 ]
  ret i8* %tmp50
}

; Function Attrs: nobuiltin nofree
declare noalias nonnull i8* @_Znam(i64) local_unnamed_addr #1

; Function Attrs: nobuiltin nounwind
declare void @_ZdaPv(i8*) local_unnamed_addr #2

declare i32 @__gxx_personality_v0(...)

; Function Attrs: sspstrong uwtable
define void @__cxa_vec_ctor(i8* %arg, i64 %arg1, i64 %arg2, void (i8*)* %arg3, void (i8*)* %arg4) local_unnamed_addr #0 personality i8* bitcast (i32 (...)* @__gxx_personality_v0 to i8*) {
bb:
  %tmp = icmp ne void (i8*)* %arg3, null
  %tmp5 = icmp ne i64 %arg1, 0
  %tmp6 = and i1 %tmp, %tmp5
  br i1 %tmp6, label %bb7, label %bb31

bb7:                                              ; preds = %bb10, %bb
  %tmp8 = phi i8* [ %tmp12, %bb10 ], [ %arg, %bb ]
  %tmp9 = phi i64 [ %tmp11, %bb10 ], [ 0, %bb ]
  invoke void %arg3(i8* %tmp8)
          to label %bb10 unwind label %bb14

bb10:                                             ; preds = %bb7
  %tmp11 = add nuw i64 %tmp9, 1
  %tmp12 = getelementptr inbounds i8, i8* %tmp8, i64 %arg2
  %tmp13 = icmp eq i64 %tmp11, %arg1
  br i1 %tmp13, label %bb31, label %bb7

bb14:                                             ; preds = %bb7
  %tmp15 = landingpad { i8*, i32 }
          cleanup
  %tmp16 = icmp eq void (i8*)* %arg4, null
  br i1 %tmp16, label %bb30, label %bb17

bb17:                                             ; preds = %bb14
  %tmp18 = mul i64 %tmp9, %arg2
  %tmp19 = getelementptr inbounds i8, i8* %arg, i64 %tmp18
  %tmp20 = sub i64 0, %arg2
  br label %bb21

bb21:                                             ; preds = %bb25, %bb17
  %tmp22 = phi i64 [ %tmp9, %bb17 ], [ %tmp26, %bb25 ]
  %tmp23 = phi i8* [ %tmp19, %bb17 ], [ %tmp27, %bb25 ]
  %tmp24 = icmp eq i64 %tmp22, 0
  br i1 %tmp24, label %bb30, label %bb25

bb25:                                             ; preds = %bb21
  %tmp26 = add i64 %tmp22, -1
  %tmp27 = getelementptr inbounds i8, i8* %tmp23, i64 %tmp20
  invoke void %arg4(i8* %tmp27)
          to label %bb21 unwind label %bb28

bb28:                                             ; preds = %bb25
  %tmp29 = landingpad { i8*, i32 }
          catch i8* null
  tail call void @_ZSt9terminatev() #5
  unreachable

bb30:                                             ; preds = %bb21, %bb14
  resume { i8*, i32 } %tmp15

bb31:                                             ; preds = %bb10, %bb
  ret void
}

; Function Attrs: sspstrong uwtable
define i8* @__cxa_vec_new3(i64 %arg, i64 %arg1, i64 %arg2, void (i8*)* %arg3, void (i8*)* %arg4, i8* (i64)* nocapture %arg5, void (i8*, i64)* %arg6) local_unnamed_addr #0 personality i8* bitcast (i32 (...)* @__gxx_personality_v0 to i8*) {
bb:
  %tmp = mul i64 %arg1, %arg
  %tmp7 = add i64 %tmp, %arg2
  %tmp8 = tail call i8* %arg5(i64 %tmp7)
  %tmp9 = icmp eq i8* %tmp8, null
  br i1 %tmp9, label %bb49, label %bb10

bb10:                                             ; preds = %bb
  %tmp11 = icmp eq i64 %arg2, 0
  br i1 %tmp11, label %bb21, label %bb12

bb12:                                             ; preds = %bb10
  %tmp13 = getelementptr inbounds i8, i8* %tmp8, i64 %arg2
  %tmp14 = getelementptr inbounds i8, i8* %tmp13, i64 -8
  %tmp15 = bitcast i8* %tmp14 to i64*
  store i64 %arg, i64* %tmp15, align 8, !tbaa !3
  br label %bb21

bb16:                                             ; preds = %bb40, %bb33
  invoke void %arg6(i8* nonnull %tmp8, i64 %tmp7)
          to label %bb20 unwind label %bb17

bb17:                                             ; preds = %bb16
  %tmp18 = landingpad { i8*, i32 }
          catch i8* null
  %tmp19 = extractvalue { i8*, i32 } %tmp18, 0
  tail call void @__clang_call_terminate(i8* %tmp19) #5
  unreachable

bb20:                                             ; preds = %bb16
  resume { i8*, i32 } %tmp34

bb21:                                             ; preds = %bb12, %bb10
  %tmp22 = phi i8* [ %tmp13, %bb12 ], [ %tmp8, %bb10 ]
  %tmp23 = icmp ne void (i8*)* %arg3, null
  %tmp24 = icmp ne i64 %arg, 0
  %tmp25 = and i1 %tmp24, %tmp23
  br i1 %tmp25, label %bb26, label %bb49

bb26:                                             ; preds = %bb29, %bb21
  %tmp27 = phi i8* [ %tmp31, %bb29 ], [ %tmp22, %bb21 ]
  %tmp28 = phi i64 [ %tmp30, %bb29 ], [ 0, %bb21 ]
  invoke void %arg3(i8* %tmp27)
          to label %bb29 unwind label %bb33

bb29:                                             ; preds = %bb26
  %tmp30 = add nuw i64 %tmp28, 1
  %tmp31 = getelementptr inbounds i8, i8* %tmp27, i64 %arg1
  %tmp32 = icmp eq i64 %tmp30, %arg
  br i1 %tmp32, label %bb49, label %bb26

bb33:                                             ; preds = %bb26
  %tmp34 = landingpad { i8*, i32 }
          cleanup
  %tmp35 = icmp eq void (i8*)* %arg4, null
  br i1 %tmp35, label %bb16, label %bb36

bb36:                                             ; preds = %bb33
  %tmp37 = mul i64 %tmp28, %arg1
  %tmp38 = getelementptr inbounds i8, i8* %tmp22, i64 %tmp37
  %tmp39 = sub i64 0, %arg1
  br label %bb40

bb40:                                             ; preds = %bb44, %bb36
  %tmp41 = phi i64 [ %tmp28, %bb36 ], [ %tmp45, %bb44 ]
  %tmp42 = phi i8* [ %tmp38, %bb36 ], [ %tmp46, %bb44 ]
  %tmp43 = icmp eq i64 %tmp41, 0
  br i1 %tmp43, label %bb16, label %bb44

bb44:                                             ; preds = %bb40
  %tmp45 = add i64 %tmp41, -1
  %tmp46 = getelementptr inbounds i8, i8* %tmp42, i64 %tmp39
  invoke void %arg4(i8* %tmp46)
          to label %bb40 unwind label %bb47

bb47:                                             ; preds = %bb44
  %tmp48 = landingpad { i8*, i32 }
          catch i8* null
  tail call void @_ZSt9terminatev() #5
  unreachable

bb49:                                             ; preds = %bb29, %bb21, %bb
  %tmp50 = phi i8* [ null, %bb ], [ %tmp22, %bb21 ], [ %tmp22, %bb29 ]
  ret i8* %tmp50
}

; Function Attrs: sspstrong uwtable
define void @__cxa_vec_cctor(i8* %arg, i8* %arg1, i64 %arg2, i64 %arg3, void (i8*, i8*)* %arg4, void (i8*)* %arg5) local_unnamed_addr #0 personality i8* bitcast (i32 (...)* @__gxx_personality_v0 to i8*) {
bb:
  %tmp = icmp ne void (i8*, i8*)* %arg4, null
  %tmp6 = icmp ne i64 %arg2, 0
  %tmp7 = and i1 %tmp, %tmp6
  br i1 %tmp7, label %bb8, label %bb34

bb8:                                              ; preds = %bb12, %bb
  %tmp9 = phi i8* [ %tmp14, %bb12 ], [ %arg1, %bb ]
  %tmp10 = phi i8* [ %tmp15, %bb12 ], [ %arg, %bb ]
  %tmp11 = phi i64 [ %tmp13, %bb12 ], [ 0, %bb ]
  invoke void %arg4(i8* %tmp10, i8* %tmp9)
          to label %bb12 unwind label %bb17

bb12:                                             ; preds = %bb8
  %tmp13 = add nuw i64 %tmp11, 1
  %tmp14 = getelementptr inbounds i8, i8* %tmp9, i64 %arg3
  %tmp15 = getelementptr inbounds i8, i8* %tmp10, i64 %arg3
  %tmp16 = icmp eq i64 %tmp13, %arg2
  br i1 %tmp16, label %bb34, label %bb8

bb17:                                             ; preds = %bb8
  %tmp18 = landingpad { i8*, i32 }
          cleanup
  %tmp19 = icmp eq void (i8*)* %arg5, null
  br i1 %tmp19, label %bb33, label %bb20

bb20:                                             ; preds = %bb17
  %tmp21 = mul i64 %tmp11, %arg3
  %tmp22 = getelementptr inbounds i8, i8* %arg, i64 %tmp21
  %tmp23 = sub i64 0, %arg3
  br label %bb24

bb24:                                             ; preds = %bb28, %bb20
  %tmp25 = phi i64 [ %tmp11, %bb20 ], [ %tmp29, %bb28 ]
  %tmp26 = phi i8* [ %tmp22, %bb20 ], [ %tmp30, %bb28 ]
  %tmp27 = icmp eq i64 %tmp25, 0
  br i1 %tmp27, label %bb33, label %bb28

bb28:                                             ; preds = %bb24
  %tmp29 = add i64 %tmp25, -1
  %tmp30 = getelementptr inbounds i8, i8* %tmp26, i64 %tmp23
  invoke void %arg5(i8* %tmp30)
          to label %bb24 unwind label %bb31

bb31:                                             ; preds = %bb28
  %tmp32 = landingpad { i8*, i32 }
          catch i8* null
  tail call void @_ZSt9terminatev() #5
  unreachable

bb33:                                             ; preds = %bb24, %bb17
  resume { i8*, i32 } %tmp18

bb34:                                             ; preds = %bb12, %bb
  ret void
}

; Function Attrs: sspstrong uwtable
define void @__cxa_vec_dtor(i8* %arg, i64 %arg1, i64 %arg2, void (i8*)* %arg3) local_unnamed_addr #0 personality i8* bitcast (i32 (...)* @__gxx_personality_v0 to i8*) {
bb:
  %tmp = icmp eq void (i8*)* %arg3, null
  br i1 %tmp, label %bb32, label %bb4

bb4:                                              ; preds = %bb
  %tmp5 = tail call zeroext i1 @__cxa_uncaught_exception() #6
  %tmp6 = mul i64 %arg2, %arg1
  %tmp7 = getelementptr inbounds i8, i8* %arg, i64 %tmp6
  %tmp8 = sub i64 0, %arg2
  br label %bb9

bb9:                                              ; preds = %bb14, %bb4
  %tmp10 = phi i64 [ %arg1, %bb4 ], [ %tmp12, %bb14 ]
  %tmp11 = phi i8* [ %tmp7, %bb4 ], [ %tmp15, %bb14 ]
  %tmp12 = add i64 %tmp10, -1
  %tmp13 = icmp eq i64 %tmp10, 0
  br i1 %tmp13, label %bb32, label %bb14

bb14:                                             ; preds = %bb9
  %tmp15 = getelementptr inbounds i8, i8* %tmp11, i64 %tmp8
  invoke void %arg3(i8* %tmp15)
          to label %bb9 unwind label %bb16

bb16:                                             ; preds = %bb14
  %tmp17 = landingpad { i8*, i32 }
          cleanup
  br i1 %tmp5, label %bb18, label %bb19

bb18:                                             ; preds = %bb16
  tail call void @_ZSt9terminatev() #5
  unreachable

bb19:                                             ; preds = %bb16
  %tmp20 = mul i64 %tmp12, %arg2
  %tmp21 = getelementptr inbounds i8, i8* %arg, i64 %tmp20
  br label %bb22

bb22:                                             ; preds = %bb26, %bb19
  %tmp23 = phi i64 [ %tmp12, %bb19 ], [ %tmp27, %bb26 ]
  %tmp24 = phi i8* [ %tmp21, %bb19 ], [ %tmp28, %bb26 ]
  %tmp25 = icmp eq i64 %tmp23, 0
  br i1 %tmp25, label %bb31, label %bb26

bb26:                                             ; preds = %bb22
  %tmp27 = add i64 %tmp23, -1
  %tmp28 = getelementptr inbounds i8, i8* %tmp24, i64 %tmp8
  invoke void %arg3(i8* %tmp28)
          to label %bb22 unwind label %bb29

bb29:                                             ; preds = %bb26
  %tmp30 = landingpad { i8*, i32 }
          catch i8* null
  tail call void @_ZSt9terminatev() #5
  unreachable

bb31:                                             ; preds = %bb22
  resume { i8*, i32 } %tmp17

bb32:                                             ; preds = %bb9, %bb
  ret void
}

; Function Attrs: nounwind
declare zeroext i1 @__cxa_uncaught_exception() local_unnamed_addr #3

; Function Attrs: sspstrong uwtable
define void @__cxa_vec_cleanup(i8* %arg, i64 %arg1, i64 %arg2, void (i8*)* %arg3) local_unnamed_addr #0 personality i8* bitcast (i32 (...)* @__gxx_personality_v0 to i8*) {
bb:
  %tmp = icmp eq void (i8*)* %arg3, null
  br i1 %tmp, label %bb17, label %bb4

bb4:                                              ; preds = %bb
  %tmp5 = mul i64 %arg2, %arg1
  %tmp6 = getelementptr inbounds i8, i8* %arg, i64 %tmp5
  %tmp7 = sub i64 0, %arg2
  br label %bb8

bb8:                                              ; preds = %bb12, %bb4
  %tmp9 = phi i64 [ %arg1, %bb4 ], [ %tmp13, %bb12 ]
  %tmp10 = phi i8* [ %tmp6, %bb4 ], [ %tmp14, %bb12 ]
  %tmp11 = icmp eq i64 %tmp9, 0
  br i1 %tmp11, label %bb17, label %bb12

bb12:                                             ; preds = %bb8
  %tmp13 = add i64 %tmp9, -1
  %tmp14 = getelementptr inbounds i8, i8* %tmp10, i64 %tmp7
  invoke void %arg3(i8* %tmp14)
          to label %bb8 unwind label %bb15

bb15:                                             ; preds = %bb12
  %tmp16 = landingpad { i8*, i32 }
          cleanup
  tail call void @_ZSt9terminatev() #5
  unreachable

bb17:                                             ; preds = %bb8, %bb
  ret void
}

; Function Attrs: sspstrong uwtable
define void @__cxa_vec_delete(i8* %arg, i64 %arg1, i64 %arg2, void (i8*)* %arg3) local_unnamed_addr #0 personality i32 (...)* @__gxx_personality_v0 {
bb:
  %tmp = icmp eq i8* %arg, null
  br i1 %tmp, label %bb42, label %bb4

bb4:                                              ; preds = %bb
  %tmp5 = sub i64 0, %arg2
  %tmp6 = getelementptr inbounds i8, i8* %arg, i64 %tmp5
  %tmp7 = icmp ne i64 %arg2, 0
  %tmp8 = icmp ne void (i8*)* %arg3, null
  %tmp9 = and i1 %tmp7, %tmp8
  br i1 %tmp9, label %bb10, label %bb41

bb10:                                             ; preds = %bb4
  %tmp11 = getelementptr inbounds i8, i8* %arg, i64 -8
  %tmp12 = bitcast i8* %tmp11 to i64*
  %tmp13 = load i64, i64* %tmp12, align 8, !tbaa !3
  %tmp14 = tail call zeroext i1 @__cxa_uncaught_exception() #6
  %tmp15 = mul i64 %tmp13, %arg1
  %tmp16 = getelementptr inbounds i8, i8* %arg, i64 %tmp15
  %tmp17 = sub i64 0, %arg1
  br label %bb18

bb18:                                             ; preds = %bb23, %bb10
  %tmp19 = phi i64 [ %tmp13, %bb10 ], [ %tmp21, %bb23 ]
  %tmp20 = phi i8* [ %tmp16, %bb10 ], [ %tmp24, %bb23 ]
  %tmp21 = add i64 %tmp19, -1
  %tmp22 = icmp eq i64 %tmp19, 0
  br i1 %tmp22, label %bb41, label %bb23

bb23:                                             ; preds = %bb18
  %tmp24 = getelementptr inbounds i8, i8* %tmp20, i64 %tmp17
  invoke void %arg3(i8* %tmp24)
          to label %bb18 unwind label %bb25

bb25:                                             ; preds = %bb23
  %tmp26 = landingpad { i8*, i32 }
          cleanup
  br i1 %tmp14, label %bb27, label %bb28

bb27:                                             ; preds = %bb25
  tail call void @_ZSt9terminatev() #5
  unreachable

bb28:                                             ; preds = %bb25
  %tmp29 = mul i64 %tmp21, %arg1
  %tmp30 = getelementptr inbounds i8, i8* %arg, i64 %tmp29
  br label %bb31

bb31:                                             ; preds = %bb35, %bb28
  %tmp32 = phi i64 [ %tmp21, %bb28 ], [ %tmp36, %bb35 ]
  %tmp33 = phi i8* [ %tmp30, %bb28 ], [ %tmp37, %bb35 ]
  %tmp34 = icmp eq i64 %tmp32, 0
  br i1 %tmp34, label %bb40, label %bb35

bb35:                                             ; preds = %bb31
  %tmp36 = add i64 %tmp32, -1
  %tmp37 = getelementptr inbounds i8, i8* %tmp33, i64 %tmp17
  invoke void %arg3(i8* %tmp37)
          to label %bb31 unwind label %bb38

bb38:                                             ; preds = %bb35
  %tmp39 = landingpad { i8*, i32 }
          catch i8* null
  tail call void @_ZSt9terminatev() #5
  unreachable

bb40:                                             ; preds = %bb31
  tail call void @_ZdaPv(i8* nonnull %tmp6)
  resume { i8*, i32 } %tmp26

bb41:                                             ; preds = %bb18, %bb4
  tail call void @_ZdaPv(i8* nonnull %tmp6)
  br label %bb42

bb42:                                             ; preds = %bb41, %bb
  ret void
}

; Function Attrs: sspstrong uwtable
define void @__cxa_vec_delete2(i8* %arg, i64 %arg1, i64 %arg2, void (i8*)* %arg3, void (i8*)* %arg4) local_unnamed_addr #0 personality i8* bitcast (i32 (...)* @__gxx_personality_v0 to i8*) {
bb:
  %tmp = icmp eq i8* %arg, null
  br i1 %tmp, label %bb50, label %bb5

bb5:                                              ; preds = %bb
  %tmp6 = sub i64 0, %arg2
  %tmp7 = getelementptr inbounds i8, i8* %arg, i64 %tmp6
  %tmp8 = icmp ne i64 %arg2, 0
  %tmp9 = icmp ne void (i8*)* %arg3, null
  %tmp10 = and i1 %tmp8, %tmp9
  br i1 %tmp10, label %bb11, label %bb46

bb11:                                             ; preds = %bb5
  %tmp12 = getelementptr inbounds i8, i8* %arg, i64 -8
  %tmp13 = bitcast i8* %tmp12 to i64*
  %tmp14 = load i64, i64* %tmp13, align 8, !tbaa !3
  %tmp15 = tail call zeroext i1 @__cxa_uncaught_exception() #6
  %tmp16 = mul i64 %tmp14, %arg1
  %tmp17 = getelementptr inbounds i8, i8* %arg, i64 %tmp16
  %tmp18 = sub i64 0, %arg1
  br label %bb19

bb19:                                             ; preds = %bb24, %bb11
  %tmp20 = phi i64 [ %tmp14, %bb11 ], [ %tmp22, %bb24 ]
  %tmp21 = phi i8* [ %tmp17, %bb11 ], [ %tmp25, %bb24 ]
  %tmp22 = add i64 %tmp20, -1
  %tmp23 = icmp eq i64 %tmp20, 0
  br i1 %tmp23, label %bb46, label %bb24

bb24:                                             ; preds = %bb19
  %tmp25 = getelementptr inbounds i8, i8* %tmp21, i64 %tmp18
  invoke void %arg3(i8* %tmp25)
          to label %bb19 unwind label %bb26

bb26:                                             ; preds = %bb24
  %tmp27 = landingpad { i8*, i32 }
          cleanup
  br i1 %tmp15, label %bb28, label %bb29

bb28:                                             ; preds = %bb26
  tail call void @_ZSt9terminatev() #5
  unreachable

bb29:                                             ; preds = %bb26
  %tmp30 = mul i64 %tmp22, %arg1
  %tmp31 = getelementptr inbounds i8, i8* %arg, i64 %tmp30
  br label %bb32

bb32:                                             ; preds = %bb36, %bb29
  %tmp33 = phi i64 [ %tmp22, %bb29 ], [ %tmp37, %bb36 ]
  %tmp34 = phi i8* [ %tmp31, %bb29 ], [ %tmp38, %bb36 ]
  %tmp35 = icmp eq i64 %tmp33, 0
  br i1 %tmp35, label %bb41, label %bb36

bb36:                                             ; preds = %bb32
  %tmp37 = add i64 %tmp33, -1
  %tmp38 = getelementptr inbounds i8, i8* %tmp34, i64 %tmp18
  invoke void %arg3(i8* %tmp38)
          to label %bb32 unwind label %bb39

bb39:                                             ; preds = %bb36
  %tmp40 = landingpad { i8*, i32 }
          catch i8* null
  tail call void @_ZSt9terminatev() #5
  unreachable

bb41:                                             ; preds = %bb32
  invoke void %arg4(i8* nonnull %tmp7)
          to label %bb45 unwind label %bb42

bb42:                                             ; preds = %bb41
  %tmp43 = landingpad { i8*, i32 }
          catch i8* null
  %tmp44 = extractvalue { i8*, i32 } %tmp43, 0
  tail call void @__clang_call_terminate(i8* %tmp44) #5
  unreachable

bb45:                                             ; preds = %bb41
  resume { i8*, i32 } %tmp27

bb46:                                             ; preds = %bb19, %bb5
  invoke void %arg4(i8* nonnull %tmp7)
          to label %bb50 unwind label %bb47

bb47:                                             ; preds = %bb46
  %tmp48 = landingpad { i8*, i32 }
          catch i8* null
  %tmp49 = extractvalue { i8*, i32 } %tmp48, 0
  tail call void @__clang_call_terminate(i8* %tmp49) #5
  unreachable

bb50:                                             ; preds = %bb46, %bb
  ret void
}

; Function Attrs: sspstrong uwtable
define void @__cxa_vec_delete3(i8* %arg, i64 %arg1, i64 %arg2, void (i8*)* %arg3, void (i8*, i64)* %arg4) local_unnamed_addr #0 personality i8* bitcast (i32 (...)* @__gxx_personality_v0 to i8*) {
bb:
  %tmp = icmp eq i8* %arg, null
  br i1 %tmp, label %bb52, label %bb5

bb5:                                              ; preds = %bb
  %tmp6 = sub i64 0, %arg2
  %tmp7 = getelementptr inbounds i8, i8* %arg, i64 %tmp6
  %tmp8 = icmp eq i64 %arg2, 0
  br i1 %tmp8, label %bb47, label %bb9

bb9:                                              ; preds = %bb5
  %tmp10 = getelementptr inbounds i8, i8* %arg, i64 -8
  %tmp11 = bitcast i8* %tmp10 to i64*
  %tmp12 = load i64, i64* %tmp11, align 8, !tbaa !3
  %tmp13 = mul i64 %tmp12, %arg1
  %tmp14 = add i64 %tmp13, %arg2
  %tmp15 = icmp eq void (i8*)* %arg3, null
  br i1 %tmp15, label %bb47, label %bb16

bb16:                                             ; preds = %bb9
  %tmp17 = tail call zeroext i1 @__cxa_uncaught_exception() #6
  %tmp18 = getelementptr inbounds i8, i8* %arg, i64 %tmp13
  %tmp19 = sub i64 0, %arg1
  br label %bb20

bb20:                                             ; preds = %bb25, %bb16
  %tmp21 = phi i64 [ %tmp12, %bb16 ], [ %tmp23, %bb25 ]
  %tmp22 = phi i8* [ %tmp18, %bb16 ], [ %tmp26, %bb25 ]
  %tmp23 = add i64 %tmp21, -1
  %tmp24 = icmp eq i64 %tmp21, 0
  br i1 %tmp24, label %bb47, label %bb25

bb25:                                             ; preds = %bb20
  %tmp26 = getelementptr inbounds i8, i8* %tmp22, i64 %tmp19
  invoke void %arg3(i8* %tmp26)
          to label %bb20 unwind label %bb27

bb27:                                             ; preds = %bb25
  %tmp28 = landingpad { i8*, i32 }
          cleanup
  br i1 %tmp17, label %bb29, label %bb30

bb29:                                             ; preds = %bb27
  tail call void @_ZSt9terminatev() #5
  unreachable

bb30:                                             ; preds = %bb27
  %tmp31 = mul i64 %tmp23, %arg1
  %tmp32 = getelementptr inbounds i8, i8* %arg, i64 %tmp31
  br label %bb33

bb33:                                             ; preds = %bb37, %bb30
  %tmp34 = phi i64 [ %tmp23, %bb30 ], [ %tmp38, %bb37 ]
  %tmp35 = phi i8* [ %tmp32, %bb30 ], [ %tmp39, %bb37 ]
  %tmp36 = icmp eq i64 %tmp34, 0
  br i1 %tmp36, label %bb42, label %bb37

bb37:                                             ; preds = %bb33
  %tmp38 = add i64 %tmp34, -1
  %tmp39 = getelementptr inbounds i8, i8* %tmp35, i64 %tmp19
  invoke void %arg3(i8* %tmp39)
          to label %bb33 unwind label %bb40

bb40:                                             ; preds = %bb37
  %tmp41 = landingpad { i8*, i32 }
          catch i8* null
  tail call void @_ZSt9terminatev() #5
  unreachable

bb42:                                             ; preds = %bb33
  invoke void %arg4(i8* nonnull %tmp7, i64 %tmp14)
          to label %bb46 unwind label %bb43

bb43:                                             ; preds = %bb42
  %tmp44 = landingpad { i8*, i32 }
          catch i8* null
  %tmp45 = extractvalue { i8*, i32 } %tmp44, 0
  tail call void @__clang_call_terminate(i8* %tmp45) #5
  unreachable

bb46:                                             ; preds = %bb42
  resume { i8*, i32 } %tmp28

bb47:                                             ; preds = %bb20, %bb9, %bb5
  %tmp48 = phi i64 [ %tmp14, %bb9 ], [ 0, %bb5 ], [ %tmp14, %bb20 ]
  invoke void %arg4(i8* nonnull %tmp7, i64 %tmp48)
          to label %bb52 unwind label %bb49

bb49:                                             ; preds = %bb47
  %tmp50 = landingpad { i8*, i32 }
          catch i8* null
  %tmp51 = extractvalue { i8*, i32 } %tmp50, 0
  tail call void @__clang_call_terminate(i8* %tmp51) #5
  unreachable

bb52:                                             ; preds = %bb47, %bb
  ret void
}

; Function Attrs: noinline noreturn nounwind
define linkonce_odr hidden void @__clang_call_terminate(i8* %arg) local_unnamed_addr #4 comdat {
bb:
  %tmp = tail call i8* @__cxa_begin_catch(i8* %arg) #6
  tail call void @_ZSt9terminatev() #5
  unreachable
}

declare i8* @__cxa_begin_catch(i8*) local_unnamed_addr

declare void @_ZSt9terminatev() local_unnamed_addr

attributes #0 = { sspstrong uwtable "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-frame-pointer-elim"="false" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { nobuiltin "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #2 = { nobuiltin nounwind "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #3 = { nounwind "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #4 = { noinline noreturn nounwind }
attributes #5 = { noreturn nounwind }
attributes #6 = { nounwind }

!llvm.module.flags = !{!0, !1}
!llvm.ident = !{!2}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 7, !"PIC Level", i32 2}
!2 = !{!"clang version 9.0.0 (tags/RELEASE_900/final)"}
!3 = !{!4, !4, i64 0}
!4 = !{!"long", !5, i64 0}
!5 = !{!"omnipotent char", !6, i64 0}
!6 = !{!"Simple C++ TBAA"}
