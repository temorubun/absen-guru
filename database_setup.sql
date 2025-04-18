-- Enable RLS (Row Level Security)
ALTER TABLE IF EXISTS public.staff ENABLE ROW LEVEL SECURITY;
ALTER TABLE IF EXISTS public.absensi ENABLE ROW LEVEL SECURITY;

-- Create Staff Table
CREATE TABLE IF NOT EXISTS public.staff (
    id UUID DEFAULT gen_random_uuid() PRIMARY KEY,
    fingerprint_id INTEGER UNIQUE NOT NULL,
    nama TEXT NOT NULL,
    jabatan TEXT NOT NULL,
    nip TEXT UNIQUE NOT NULL,
    status TEXT DEFAULT 'aktif' CHECK (status IN ('aktif', 'nonaktif')),
    created_at TIMESTAMP WITH TIME ZONE DEFAULT timezone('Asia/Jakarta'::text, now()),
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT timezone('Asia/Jakarta'::text, now())
);

-- Create Absensi Table
CREATE TABLE IF NOT EXISTS public.absensi (
    id UUID DEFAULT gen_random_uuid() PRIMARY KEY,
    staff_id UUID REFERENCES public.staff(id) ON DELETE CASCADE,
    fingerprint_id INTEGER NOT NULL,
    tanggal DATE DEFAULT CURRENT_DATE,
    waktu_masuk TIMESTAMP WITH TIME ZONE DEFAULT timezone('Asia/Jakarta'::text, now()),
    waktu_keluar TIMESTAMP WITH TIME ZONE,
    status_kehadiran TEXT DEFAULT 'hadir' CHECK (status_kehadiran IN ('hadir', 'terlambat', 'izin', 'sakit', 'alpha')),
    keterangan TEXT,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT timezone('Asia/Jakarta'::text, now())
);

-- Create Department Table
CREATE TABLE IF NOT EXISTS public.department (
    id UUID DEFAULT gen_random_uuid() PRIMARY KEY,
    nama_department TEXT NOT NULL UNIQUE,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT timezone('Asia/Jakarta'::text, now())
);

-- Add Department Reference to Staff
ALTER TABLE public.staff 
ADD COLUMN IF NOT EXISTS department_id UUID REFERENCES public.department(id);

-- Create Shift Table
CREATE TABLE IF NOT EXISTS public.shift (
    id UUID DEFAULT gen_random_uuid() PRIMARY KEY,
    nama_shift TEXT NOT NULL,
    jam_masuk TIME NOT NULL,
    jam_keluar TIME NOT NULL,
    toleransi_keterlambatan INTEGER DEFAULT 15, -- dalam menit
    created_at TIMESTAMP WITH TIME ZONE DEFAULT timezone('Asia/Jakarta'::text, now())
);

-- Add Shift Reference to Staff
ALTER TABLE public.staff 
ADD COLUMN IF NOT EXISTS shift_id UUID REFERENCES public.shift(id);

-- Create Function to Update Status Kehadiran
CREATE OR REPLACE FUNCTION update_status_kehadiran()
RETURNS TRIGGER AS $$
DECLARE
    staff_shift_record RECORD;
    batas_terlambat TIME;
BEGIN
    -- Get staff shift information
    SELECT s.jam_masuk, s.toleransi_keterlambatan 
    INTO staff_shift_record
    FROM staff st
    JOIN shift s ON s.id = st.shift_id
    WHERE st.id = NEW.staff_id;

    -- Calculate late threshold
    batas_terlambat := staff_shift_record.jam_masuk + 
                       (staff_shift_record.toleransi_keterlambatan || ' minutes')::interval;

    -- Update status based on entry time
    IF CAST(NEW.waktu_masuk AS TIME) > batas_terlambat THEN
        NEW.status_kehadiran := 'terlambat';
    END IF;

    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

-- Create Trigger for Status Kehadiran
CREATE TRIGGER tr_update_status_kehadiran
BEFORE INSERT ON public.absensi
FOR EACH ROW
EXECUTE FUNCTION update_status_kehadiran();

-- Create View for Daily Attendance Report
CREATE OR REPLACE VIEW daily_attendance_report AS
SELECT 
    a.tanggal,
    s.nama,
    s.nip,
    s.jabatan,
    d.nama_department,
    sh.nama_shift,
    a.waktu_masuk,
    a.waktu_keluar,
    a.status_kehadiran,
    a.keterangan
FROM absensi a
JOIN staff s ON s.id = a.staff_id
LEFT JOIN department d ON d.id = s.department_id
LEFT JOIN shift sh ON sh.id = s.shift_id
ORDER BY a.tanggal DESC, a.waktu_masuk DESC;

-- Create View for Monthly Attendance Summary
CREATE OR REPLACE VIEW monthly_attendance_summary AS
SELECT 
    s.nama,
    s.nip,
    s.jabatan,
    d.nama_department,
    DATE_TRUNC('month', a.tanggal) AS bulan,
    COUNT(*) FILTER (WHERE a.status_kehadiran = 'hadir') AS jumlah_hadir,
    COUNT(*) FILTER (WHERE a.status_kehadiran = 'terlambat') AS jumlah_terlambat,
    COUNT(*) FILTER (WHERE a.status_kehadiran = 'izin') AS jumlah_izin,
    COUNT(*) FILTER (WHERE a.status_kehadiran = 'sakit') AS jumlah_sakit,
    COUNT(*) FILTER (WHERE a.status_kehadiran = 'alpha') AS jumlah_alpha
FROM staff s
LEFT JOIN absensi a ON a.staff_id = s.id
LEFT JOIN department d ON d.id = s.department_id
GROUP BY s.nama, s.nip, s.jabatan, d.nama_department, DATE_TRUNC('month', a.tanggal)
ORDER BY DATE_TRUNC('month', a.tanggal) DESC;

-- Insert Default Department
INSERT INTO public.department (nama_department) 
VALUES ('Guru'), ('Staf Administrasi'), ('Penjaga Sekolah')
ON CONFLICT (nama_department) DO NOTHING;

-- Insert Default Shifts
INSERT INTO public.shift (nama_shift, jam_masuk, jam_keluar) 
VALUES 
    ('Pagi', '07:00', '15:00'),
    ('Siang', '13:00', '21:00')
ON CONFLICT DO NOTHING;

-- Create Policies
CREATE POLICY "Enable read access for all users" ON "public"."staff"
FOR SELECT USING (true);

CREATE POLICY "Enable read access for all users" ON "public"."absensi"
FOR SELECT USING (true);

CREATE POLICY "Enable read access for all users" ON "public"."department"
FOR SELECT USING (true);

CREATE POLICY "Enable read access for all users" ON "public"."shift"
FOR SELECT USING (true);

-- Create Indexes
CREATE INDEX IF NOT EXISTS idx_absensi_tanggal ON public.absensi(tanggal);
CREATE INDEX IF NOT EXISTS idx_absensi_staff_id ON public.absensi(staff_id);
CREATE INDEX IF NOT EXISTS idx_staff_fingerprint_id ON public.staff(fingerprint_id);
CREATE INDEX IF NOT EXISTS idx_staff_department ON public.staff(department_id);
CREATE INDEX IF NOT EXISTS idx_staff_shift ON public.staff(shift_id); 
