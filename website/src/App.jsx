import { BrowserRouter as Router, Routes, Route, Navigate } from 'react-router-dom';
import LandingPage from './pages/LandingPage';
import ProductsPage from './pages/ProductsPage';
import DashboardPage from './pages/dashboard/DashboardPage';
import AdminPanel from './pages/admin/AdminPanel';
import MediaPage from './pages/MediaPage';
import DistributorPage from './pages/DistributorPage';

function App() {
  return (
    <Router>
      <Routes>
        {/* Public Routes */}
        <Route path="/" element={<LandingPage />} />
        <Route path="/games/:game" element={<ProductsPage />} />
        
        {/* Dashboard Route */}
        <Route path="/home" element={<DashboardPage />} />
        
        {/* Media & Distributor */}
        <Route path="/media" element={<MediaPage />} />
        <Route path="/distributor" element={<DistributorPage />} />
        
        {/* Admin Route */}
        <Route path="/admin444" element={<AdminPanel />} />
        <Route path="/admin" element={<Navigate to="/" replace />} />
      </Routes>
    </Router>
  );
}

export default App;
