SET search_path = public;

DROP LANGUAGE plv8u;
DROP FUNCTION plv8u_call_handler();
DROP FUNCTION plv8u_inline_handler(internal);
DROP FUNCTION plv8u_call_validator(oid);

DROP DOMAIN plv8u_int2array;
DROP DOMAIN plv8u_int4array;
DROP DOMAIN plv8u_float4array;
DROP DOMAIN plv8u_float8array;
