--  This file is part of the SymCC runtime.

--  The SymCC runtime is free software: you can redistribute it and/or modify
--  it under the terms of the GNU Lesser General Public License as published by
--  the Free Software Foundation, either version 3 of the License, or (at your
--  option) any later version.

--  The SymCC runtime is distributed in the hope that it will be useful, but
--  WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
--  or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public
--  License for more details.

with Interfaces.C;
with System;

--  @summary
--  Ada bindings to the SymCC runtime API.
--
--  @description
--  This package provides thin bindings to the user-facing functionality of the
--  SymCC runtime (see RuntimeCommon.h).
package SymCC is

   procedure Make_Symbolic
     (Address : System.Address; Size : Interfaces.C.size_t) with
     Import => True, Convention => C, External_Name => "symcc_make_symbolic";
   --  Mark a memory region as symbolic program input.
   --  @param Address The start of the region containing the input data.
   --  @param Size The length in bytes of the region.

   type Test_Case_Handler_Callback_Type is
     access procedure
       (Data_Block : System.Address; Size : Interfaces.C.size_t) with
     Convention => C;
   --  Type of functions that the runtime can call when it generates new
   --  program inputs (see Set_Test_Case_Handler).

   procedure Set_Test_Case_Handler
     (Callback : Test_Case_Handler_Callback_Type) with
     Import        => True, Convention => C,
     External_Name => "symcc_set_test_case_handler";
   --  Define a custom handler for new program inputs.
   --  @param Callback The procedure to be called for each new input.

end SymCC;
