-- ==========================================
-- SCRIPT DE BASE DE DATOS PARA SUPABASE (Actualizado con estadísticas y remera)
-- Ejecutar este script en el SQL Editor de Supabase
-- ==========================================

-- 1. Tabla de Perfiles de Jugador (profiles)
create table if not exists public.profiles (
  id uuid references auth.users on delete cascade primary key,
  name text not null,
  avatar_url text,
  zone text,
  updated_at timestamp with time zone default timezone('utc'::text, now()) not null
);

-- Habilitar RLS para profiles
alter table public.profiles enable row level security;

-- Limpiar políticas existentes para evitar errores al re-ejecutar
drop policy if exists "Permitir lectura publica de perfiles" on public.profiles;
drop policy if exists "Permitir a los usuarios modificar su propio perfil" on public.profiles;

-- Políticas de Seguridad RLS para profiles
create policy "Permitir lectura publica de perfiles" on public.profiles
  for select to public using (true);

create policy "Permitir a los usuarios modificar su propio perfil" on public.profiles
  for all to authenticated
  using ((select auth.uid()) = id)
  with check ((select auth.uid()) = id);

-- Columnas adicionales de personalización y estadísticas
alter table public.profiles
  add column if not exists age integer,
  add column if not exists club text,
  add column if not exists zone text,
  add column if not exists jersey_primary_color text default '#ffffff',
  add column if not exists jersey_secondary_color text default '#00ffcc',
  add column if not exists jersey_pattern text default 'plain',
  add column if not exists games_played integer default 0,
  add column if not exists high_score integer default 0,
  add column if not exists current_streak integer default 0,
  add column if not exists best_streak integer default 0,
  add column if not exists total_score integer default 0,
  add column if not exists updated_at timestamptz default now();

-- 3b. Storage: Políticas RLS del bucket 'avatars'
-- (Ejecutar después de crear el bucket 'avatars' como Public en el dashboard)
create policy if not exists "Public read avatars" on storage.objects
  for select to public using (bucket_id = 'avatars');

create policy if not exists "Auth users upload avatars" on storage.objects
  for insert to authenticated
  with check (bucket_id = 'avatars' and (storage.foldername(name))[1] = (select auth.uid()::text));

create policy if not exists "Auth users update own avatars" on storage.objects
  for update to authenticated
  using (bucket_id = 'avatars' and (storage.foldername(name))[1] = (select auth.uid()::text));

create policy if not exists "Auth users delete own avatars" on storage.objects
  for delete to authenticated
  using (bucket_id = 'avatars' and (storage.foldername(name))[1] = (select auth.uid()::text));


create table if not exists public.rankings (
  id bigint generated always as identity primary key,
  created_at timestamp with time zone default timezone('utc'::text, now()) not null,
  arcade_id text not null,
  player_name text not null,
  score integer not null,
  zone text,
  is_guest boolean default true,
  google_id uuid references auth.users(id) on delete set null,
  avatar_url text,
  is_winner boolean default false
);

-- Asegurar columna is_winner si la tabla ya existía
alter table public.rankings
  add column if not exists is_winner boolean default false;

-- Habilitar RLS para rankings
alter table public.rankings enable row level security;

-- Limpiar políticas existentes para evitar errores al re-ejecutar
drop policy if exists "Permitir lectura publica de rankings" on public.rankings;
drop policy if exists "Permitir insert publico de rankings" on public.rankings;

-- Políticas de Seguridad RLS para rankings
create policy "Permitir lectura publica de rankings" on public.rankings
  for select to public using (true);

create policy "Permitir insert publico de rankings" on public.rankings
  for insert to public with check (true);

-- 3. Trigger de Postgres para actualizar estadísticas automáticamente
-- NOTA: Se usa una variable local (v_new_streak) para evitar el bug de Postgres
-- donde todas las expresiones en un UPDATE se evalúan con los valores ORIGINALES.
-- Sin esto, best_streak nunca captura el valor recién incrementado de current_streak.
create or replace function public.update_profile_stats()
returns trigger as $$
declare
  v_new_streak integer;
begin
  if new.google_id is not null then
    -- Calcular la nueva racha ANTES del UPDATE, usando una variable local
    select case
      when new.is_winner = true then coalesce(current_streak, 0) + 1
      else 0
    end
    into v_new_streak
    from public.profiles
    where id = new.google_id;

    -- Si no encontró el perfil, usar 0 como base
    if v_new_streak is null then
      v_new_streak := case when new.is_winner = true then 1 else 0 end;
    end if;

    update public.profiles
    set
      games_played   = coalesce(games_played, 0) + 1,
      high_score     = greatest(coalesce(high_score, 0), new.score),
      total_score    = coalesce(total_score, 0) + new.score,
      current_streak = v_new_streak,
      best_streak    = greatest(coalesce(best_streak, 0), v_new_streak),
      updated_at     = now()
    where id = new.google_id;
  end if;
  return new;
end;
$$ language plpgsql security definer;

-- Drop trigger si existe antes de crearlo
drop trigger if exists on_ranking_inserted on public.rankings;

create trigger on_ranking_inserted
  after insert on public.rankings
  for each row execute function public.update_profile_stats();

-- 4. Tabla de Máquinas (machines)
create table if not exists public.machines (
  arcade_id text primary key,
  name text not null,
  latitude double precision,
  longitude double precision,
  zone text,
  updated_at timestamp with time zone default timezone('utc'::text, now()) not null
);

-- Habilitar RLS para machines
alter table public.machines enable row level security;

drop policy if exists "Permitir lectura publica de maquinas" on public.machines;
drop policy if exists "Permitir insert/update publico de maquinas" on public.machines;

create policy "Permitir lectura publica de maquinas" on public.machines
  for select to public using (true);

create policy "Permitir insert/update publico de maquinas" on public.machines
  for all to public using (true) with check (true);

-- 5. Función de Postgres (RPC) para calcular el Ranking Zonal (Fórmula de Haversine en SQL puro)
create or replace function public.get_zonal_rankings(
  machine_lat double precision,
  machine_lon double precision,
  radius_km double precision
)
returns table (
  player_name text,
  score integer,
  zone text,
  avatar_url text,
  arcade_id text,
  distance double precision
) as $$
begin
  return query
  with zonal_grouped as (
    select 
      r.google_id,
      max(r.player_name) as player_name,
      max(r.score) as score,
      max(r.zone) as zone,
      max(coalesce(p.avatar_url, r.avatar_url)) as avatar_url,
      max(r.arcade_id) as arcade_id,
      min(6371 * acos(
        cos(radians(machine_lat)) * cos(radians(m.latitude)) * 
        cos(radians(m.longitude) - radians(machine_lon)) + 
        sin(radians(machine_lat)) * sin(radians(m.latitude))
      )) as distance
    from public.rankings r
    join public.machines m on r.arcade_id = m.arcade_id
    left join public.profiles p on r.google_id = p.id
    where (6371 * acos(
      cos(radians(machine_lat)) * cos(radians(m.latitude)) * 
      cos(radians(m.longitude) - radians(machine_lon)) + 
      sin(radians(machine_lat)) * sin(radians(m.latitude))
    )) <= radius_km
      and r.google_id is not null
    group by r.google_id
  )
  select 
    zg.player_name,
    zg.score,
    zg.zone,
    zg.avatar_url,
    zg.arcade_id,
    zg.distance
  from zonal_grouped zg
  order by zg.score desc
  limit 10;
end;
$$ language plpgsql security definer;

-- 6. Crear Vista de Efectividad (Rendimiento)
create or replace view public.effectiveness_ranking as
select 
  id,
  name,
  avatar_url,
  zone,
  games_played,
  (coalesce(total_score, 0)::double precision / nullif(games_played, 0)) as avg_score
from public.profiles
where games_played > 0;

-- 7. Crear Vista de Rankings vinculada con Perfiles (agrupado por jugador para evitar duplicados en ranking)
create or replace view public.rankings_view as
select 
  min(r.id) as id,
  max(r.created_at) as created_at,
  r.arcade_id,
  max(r.player_name) as player_name,
  max(r.score) as score,
  max(r.zone) as zone,
  r.google_id,
  max(coalesce(p.avatar_url, r.avatar_url)) as avatar_url
from public.rankings r
left join public.profiles p on r.google_id = p.id
where r.google_id is not null
group by r.arcade_id, r.google_id;
